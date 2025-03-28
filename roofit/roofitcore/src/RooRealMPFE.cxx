/// \cond ROOFIT_INTERNAL

/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 * @(#)root/roofitcore:$Id$
 * Authors:                                                                  *
 *   WV, Wouter Verkerke, UC Santa Barbara, verkerke@slac.stanford.edu       *
 *   DK, David Kirkby,    UC Irvine,         dkirkby@uci.edu                 *
 *                                                                           *
 * Copyright (c) 2000-2005, Regents of the University of California          *
 *                          and Stanford University. All rights reserved.    *
 *                                                                           *
 * Redistribution and use in source and binary forms,                        *
 * with or without modification, are permitted according to the terms        *
 * listed in LICENSE (http://roofit.sourceforge.net/license.txt)             *
 *****************************************************************************/

/**
\file RooRealMPFE.cxx
\class RooRealMPFE
\ingroup Roofitcore

Multi-processor front-end for parallel calculation
of RooAbsReal objects. Each RooRealMPFE forks a process that calculates
the value of the proxies RooAbsReal object. The (re)calculation of
the proxied object is started asynchronously with the calculate() option.
A subsequent call to getVal() will return the calculated value when available
If the calculation is still in progress when getVal() is called it blocks
the calling process until the calculation is done. The forked calculation process
is terminated when the front-end object is deleted
Simple use demonstration

~~~{.cpp}
RooAbsReal* slowFunc ;

double val = slowFunc->getVal() // Evaluate slowFunc in current process

RooRealMPFE mpfe("mpfe","frontend to slowFunc",*slowFunc) ;
mpfe.calculate() ;           // Start calculation of slow-func in remote process
                             // .. do other stuff here ..
double val = mpfe.getVal() // Wait for remote calculation to finish and retrieve value
~~~

For general multiprocessing in ROOT, please refer to the TProcessExecutor class.

**/

#include "Riostream.h"

#ifndef _WIN32
#include "BidirMMapPipe.h"
#endif

#include <cstdlib>
#include <sstream>
#include "RooRealMPFE.h"
#include "RooArgSet.h"
#include "RooAbsCategory.h"
#include "RooRealVar.h"
#include "RooCategory.h"
#include "RooMsgService.h"
#include "RooNLLVar.h"
#include "RooTrace.h"

#include "Rtypes.h"
#include "TSystem.h"


class RooRealMPFE ;

// RooMPSentinel is a singleton class that keeps track of all
// parallel execution processes for goodness-of-fit calculations.
// The primary task of RooMPSentinel is to terminate all server processes
// when the main ROOT process is exiting.
struct RooMPSentinel {

  static RooMPSentinel& instance();

  ~RooMPSentinel();

  void add(RooRealMPFE& mpfe) ;
  void remove(RooRealMPFE& mpfe) ;

  RooArgSet _mpfeSet ;
};

RooMPSentinel& RooMPSentinel::instance() {
  static RooMPSentinel inst;
  return inst;
}


using std::string, std::ostringstream, std::list;
using namespace RooFit;


////////////////////////////////////////////////////////////////////////////////
/// Construct front-end object for object 'arg' whose evaluation will be calculated
/// asynchronously in a separate process. If calcInline is true the value of 'arg'
/// is calculate synchronously in the current process.

RooRealMPFE::RooRealMPFE(const char *name, const char *title, RooAbsReal& arg, bool calcInline) :
  RooAbsReal(name,title),
  _state(Initialize),
  _arg("arg","arg",this,arg),
  _vars("vars","vars",this),
  _calcInProgress(false),
  _verboseClient(false),
  _verboseServer(false),
  _inlineMode(calcInline),
  _remoteEvalErrorLoggingState(RooAbsReal::PrintErrors),
  _pipe(nullptr),
  _updateMaster(nullptr),
  _retrieveDispatched(false), _evalCarry(0.)
{
#ifdef _WIN32
  _inlineMode = true;
#endif
  initVars() ;
  RooMPSentinel::instance().add(*this) ;

}



////////////////////////////////////////////////////////////////////////////////
/// Copy constructor. Initializes in clean state so that upon eval
/// this instance will create its own server processes

RooRealMPFE::RooRealMPFE(const RooRealMPFE& other, const char* name) :
  RooAbsReal(other, name),
  _state(Initialize),
  _arg("arg",this,other._arg),
  _vars("vars",this,other._vars),
  _calcInProgress(false),
  _verboseClient(other._verboseClient),
  _verboseServer(other._verboseServer),
  _inlineMode(other._inlineMode),
  _forceCalc(other._forceCalc),
  _remoteEvalErrorLoggingState(other._remoteEvalErrorLoggingState),
  _pipe(nullptr),
  _updateMaster(nullptr),
  _retrieveDispatched(false), _evalCarry(other._evalCarry)
{
  initVars() ;
  RooMPSentinel::instance().add(*this) ;
}



////////////////////////////////////////////////////////////////////////////////
/// Destructor

RooRealMPFE::~RooRealMPFE()
{
  if (_state==Client) standby();
  RooMPSentinel::instance().remove(*this);
}



////////////////////////////////////////////////////////////////////////////////
/// Initialize list of variables of front-end argument 'arg'

void RooRealMPFE::initVars()
{
  // Empty current lists
  _vars.removeAll() ;
  _saveVars.removeAll() ;

  // Retrieve non-constant parameters
  auto vars = _arg->getParameters(RooArgSet());
  // RooArgSet *ncVars = vars->selectByAttrib("Constant", false);
  RooArgList varList(*vars) ;

  // Save in lists
  _vars.add(varList) ;
  _saveVars.addClone(varList) ;
  _valueChanged.resize(_vars.size()) ;
  _constChanged.resize(_vars.size()) ;

  // Force next calculation
  _forceCalc = true ;
}

double RooRealMPFE::getCarry() const
{
  if (_inlineMode) {
    RooAbsTestStatistic* tmp = dynamic_cast<RooAbsTestStatistic*>(_arg.absArg());
    if (tmp) return tmp->getCarry();
    else return 0.;
  } else {
    return _evalCarry;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize the remote process and message passing
/// pipes between current process and remote process

void RooRealMPFE::initialize()
{
  // Trivial case: Inline mode
  if (_inlineMode) {
    _state = Inline ;
    return ;
  }

#ifndef _WIN32
  // Clear eval error log prior to forking
  // to avoid confusions...
  clearEvalErrorLog() ;
  // Fork server process and setup IPC
  _pipe = new BidirMMapPipe();

  if (_pipe->isChild()) {
    // Start server loop
    RooTrace::callgrind_zero() ;
    _state = Server ;
    serverLoop();

    // Kill server at end of service
    if (_verboseServer) ccoutD(Minimization) << "RooRealMPFE::initialize(" <<
   GetName() << ") server process terminating" << std::endl ;

    delete _arg.absArg();
    delete _pipe;
    _exit(0) ;
  } else {
    // Client process - fork successful
    if (_verboseClient) {
       ccoutD(Minimization) << "RooRealMPFE::initialize(" << GetName() << ") successfully forked server process "
                            << _pipe->pidOtherEnd() << std::endl;
    }
    _state = Client ;
    _calcInProgress = false ;
  }
#endif // _WIN32
}



////////////////////////////////////////////////////////////////////////////////
/// Server loop of remote processes. This function will return
/// only when an incoming TERMINATE message is received.

void RooRealMPFE::serverLoop()
{
#ifndef _WIN32
  int msg ;

  Int_t idx;
  Int_t index;
  Int_t numErrors;
  double value ;
  bool isConst ;

  clearEvalErrorLog() ;

  while(*_pipe && !_pipe->eof()) {
    *_pipe >> msg;
    if (Terminate == msg) {
      if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
                << ") IPC fromClient> Terminate" << std::endl;
      // send terminate acknowledged to client
      *_pipe << msg << BidirMMapPipe::flush;
      break;
    }

    switch (msg) {
    case SendReal:
      {
   *_pipe >> idx >> value >> isConst;
   if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
             << ") IPC fromClient> SendReal [" << idx << "]=" << value << std::endl ;
   RooRealVar* rvar = static_cast<RooRealVar*>(_vars.at(idx)) ;
   rvar->setVal(value) ;
   if (rvar->isConstant() != isConst) {
     rvar->setConstant(isConst) ;
   }
      }
      break ;

    case SendCat:
      {
   *_pipe >> idx >> index;
   if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
             << ") IPC fromClient> SendCat [" << idx << "]=" << index << std::endl ;
   (static_cast<RooCategory*>(_vars.at(idx)))->setIndex(index) ;
      }
      break ;

    case Calculate:
      if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
                << ") IPC fromClient> Calculate" << std::endl ;
      _value = _arg ;
      break ;

    case CalculateNoOffset:
      if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
                << ") IPC fromClient> Calculate" << std::endl ;

      RooAbsReal::setHideOffset(false) ;
      _value = _arg ;
      RooAbsReal::setHideOffset(true) ;
      break ;

    case Retrieve:
      {
   if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
             << ") IPC fromClient> Retrieve" << std::endl ;
   msg = ReturnValue;
   numErrors = numEvalErrors();
   *_pipe << msg << _value << getCarry() << numErrors;

   if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
             << ") IPC toClient> ReturnValue " << _value << " NumError " << numErrors << std::endl ;

   if (numErrors) {
     // Loop over errors
     std::string objidstr;
     {
       ostringstream oss2;
       // Format string with object identity as this cannot be evaluated on the other side
       oss2 << "PID" << gSystem->GetPid() << "/";
       printStream(oss2,kName|kClassName|kArgs,kInline);
       objidstr = oss2.str();
     }
     std::map<const RooAbsArg*,std::pair<string,list<EvalError> > >::const_iterator iter = evalErrorIter();
     const RooAbsArg* ptr = nullptr;
     for (int i = 0; i < numEvalErrorItems(); ++i) {
       list<EvalError>::const_iterator iter2 = iter->second.second.begin();
       for (; iter->second.second.end() != iter2; ++iter2) {
         ptr = iter->first;
         *_pipe << ptr << iter2->_msg << iter2->_srvval << objidstr;
         if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
      << ") IPC toClient> sending error log Arg " << iter->first << " Msg " << iter2->_msg << std::endl ;
       }
     }
     // let other end know that we're done with the list of errors
     ptr = nullptr;
     *_pipe << ptr;
     // Clear error list on local side
     clearEvalErrorLog();
   }
   *_pipe << BidirMMapPipe::flush;
      }
      break;

    case ConstOpt:
      {
   bool doTrack ;
   int code;
   *_pipe >> code >> doTrack;
   if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
             << ") IPC fromClient> ConstOpt " << code << " doTrack = " << (doTrack?"T":"F") << std::endl ;
   ((RooAbsReal&)_arg.arg()).constOptimizeTestStatistic(static_cast<RooAbsArg::ConstOpCode>(code),doTrack) ;
   break ;
      }

    case Verbose:
      {
      bool flag ;
      *_pipe >> flag;
      if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
                << ") IPC fromClient> Verbose " << (flag?1:0) << std::endl ;
      _verboseServer = flag ;
      }
      break ;


    case ApplyNLLW2:
      {
      bool flag ;
      *_pipe >> flag;
      if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
                << ") IPC fromClient> ApplyNLLW2 " << (flag?1:0) << std::endl ;

      // Do application of weight-squared here
      doApplyNLLW2(flag) ;
      }
      break ;

    case EnableOffset:
      {
      bool flag ;
      *_pipe >> flag;
      if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
                << ") IPC fromClient> EnableOffset " << (flag?1:0) << std::endl ;

      // Enable likelihoof offsetting here
      ((RooAbsReal&)_arg.arg()).enableOffsetting(flag) ;
      }
      break ;

    case LogEvalError:
      {
   int iflag2;
   *_pipe >> iflag2;
   RooAbsReal::ErrorLoggingMode flag2 = static_cast<RooAbsReal::ErrorLoggingMode>(iflag2);
   RooAbsReal::setEvalErrorLoggingMode(flag2) ;
   if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
             << ") IPC fromClient> LogEvalError flag = " << flag2 << std::endl ;
      }
      break ;


    default:
      if (_verboseServer) std::cout << "RooRealMPFE::serverLoop(" << GetName()
                << ") IPC fromClient> Unknown message (code = " << msg << ")" << std::endl ;
      break ;
    }
  }

#endif // _WIN32
}



////////////////////////////////////////////////////////////////////////////////
/// Client-side function that instructs server process to start
/// asynchronous (re)calculation of function value. This function
/// returns immediately. The calculated value can be retrieved
/// using getVal()

void RooRealMPFE::calculate() const
{

  // Start asynchronous calculation of arg value
  if (_state==Initialize) {
    //     std::cout << "RooRealMPFE::calculate(" << GetName() << ") initializing" << std::endl ;
    const_cast<RooRealMPFE*>(this)->initialize() ;
  }

  // Inline mode -- Calculate value now
  if (_state==Inline) {
    //     std::cout << "RooRealMPFE::calculate(" << GetName() << ") performing Inline calculation NOW" << std::endl ;
    _value = _arg ;
    clearValueDirty() ;
  }

#ifndef _WIN32
  // Compare current value of variables with saved values and send changes to server
  if (_state==Client) {
    //     std::cout << "RooRealMPFE::calculate(" << GetName() << ") state is Client trigger remote calculation" << std::endl ;
    Int_t i(0) ;

    //for (i=0 ; i<_vars.size() ; i++) {
    RooAbsArg *var;
    RooAbsArg *saveVar;
    for (std::size_t j=0 ; j<_vars.size() ; j++) {
      var = _vars.at(j);
      saveVar = _saveVars.at(j);

      //bool valChanged = !(*var==*saveVar) ;
      bool valChanged;
      bool constChanged;
      if (!_updateMaster) {
   valChanged = !var->isIdentical(*saveVar,true) ;
   constChanged = (var->isConstant() != saveVar->isConstant()) ;
   _valueChanged[i] = valChanged ;
   _constChanged[i] = constChanged ;
      } else {
   valChanged = _updateMaster->_valueChanged[i] ;
   constChanged = _updateMaster->_constChanged[i] ;
      }

      if ( valChanged || constChanged || _forceCalc) {
   //cout << "RooRealMPFE::calculate(" << GetName() << " variable " << var->GetName() << " changed " << std::endl ;
   if (_verboseClient) std::cout << "RooRealMPFE::calculate(" << GetName()
             << ") variable " << _vars.at(i)->GetName() << " changed" << std::endl ;
   if (constChanged) {
     (static_cast<RooRealVar*>(saveVar))->setConstant(var->isConstant()) ;
   }
   saveVar->copyCache(var) ;

   // send message to server
   if (dynamic_cast<RooAbsReal*>(var)) {
     int msg = SendReal ;
     double val = (static_cast<RooAbsReal*>(var))->getVal() ;
     bool isC = var->isConstant() ;
     *_pipe << msg << i << val << isC;

     if (_verboseServer) std::cout << "RooRealMPFE::calculate(" << GetName()
               << ") IPC toServer> SendReal [" << i << "]=" << val << (isC?" (Constant)":"") <<  std::endl ;
   } else if (dynamic_cast<RooAbsCategory*>(var)) {
     int msg = SendCat ;
     UInt_t idx = (static_cast<RooAbsCategory*>(var))->getCurrentIndex() ;
     *_pipe << msg << i << idx;
     if (_verboseServer) std::cout << "RooRealMPFE::calculate(" << GetName()
               << ") IPC toServer> SendCat [" << i << "]=" << idx << std::endl ;
   }
      }
      i++ ;
    }

    int msg = hideOffset() ? Calculate : CalculateNoOffset;
    *_pipe << msg;
    if (_verboseServer) std::cout << "RooRealMPFE::calculate(" << GetName()
              << ") IPC toServer> Calculate " << std::endl ;

    // Clear dirty state and mark that calculation request was dispatched
    clearValueDirty() ;
    _calcInProgress = true ;
    _forceCalc = false ;

    msg = Retrieve ;
    *_pipe << msg << BidirMMapPipe::flush;
    if (_verboseServer) std::cout << "RooRealMPFE::evaluate(" << GetName()
              << ") IPC toServer> Retrieve " << std::endl ;
    _retrieveDispatched = true ;

  } else if (_state!=Inline) {
    std::cout << "RooRealMPFE::calculate(" << GetName()
    << ") ERROR not in Client or Inline mode" << std::endl ;
  }


#endif // _WIN32
}




////////////////////////////////////////////////////////////////////////////////
/// If value needs recalculation and calculation has not been started
/// with a call to calculate() start it now. This function blocks
/// until remote process has finished calculation and returns
/// remote value

double RooRealMPFE::getValV(const RooArgSet* /*nset*/) const
{

  if (isValueDirty()) {
    // Cache is dirty, no calculation has been started yet
    //cout << "RooRealMPFE::getValF(" << GetName() << ") cache is dirty, calling calculate and evaluate" << std::endl ;
    calculate() ;
    _value = evaluate() ;
  } else if (_calcInProgress) {
    //cout << "RooRealMPFE::getValF(" << GetName() << ") calculation in progress, calling evaluate" << std::endl ;
    // Cache is clean and calculation is in progress
    _value = evaluate() ;
  } else {
    //cout << "RooRealMPFE::getValF(" << GetName() << ") cache is clean, doing nothing" << std::endl ;
    // Cache is clean and calculated value is in cache
  }

//   std::cout << "RooRealMPFE::getValV(" << GetName() << ") value = " << Form("%5.10f",_value) << std::endl ;
  return _value ;
}



////////////////////////////////////////////////////////////////////////////////
/// Send message to server process to retrieve output value
/// If error were logged use logEvalError() on remote side
/// transfer those errors to the local eval error queue.

double RooRealMPFE::evaluate() const
{
  // Retrieve value of arg
  double return_value = 0;
  if (_state==Inline) {
    return_value = _arg ;
  } else if (_state==Client) {
#ifndef _WIN32
    bool needflush = false;
    int msg;
    double value;

    // If current error logging state is not the same as remote state
    // update the remote state
    if (evalErrorLoggingMode() != _remoteEvalErrorLoggingState) {
      msg = LogEvalError ;
      RooAbsReal::ErrorLoggingMode flag = evalErrorLoggingMode() ;
      *_pipe << msg << flag;
      needflush = true;
      _remoteEvalErrorLoggingState = evalErrorLoggingMode() ;
    }

    if (!_retrieveDispatched) {
      msg = Retrieve ;
      *_pipe << msg;
      needflush = true;
      if (_verboseServer) std::cout << "RooRealMPFE::evaluate(" << GetName()
                << ") IPC toServer> Retrieve " << std::endl ;
    }
    if (needflush) *_pipe << BidirMMapPipe::flush;
    _retrieveDispatched = false ;


    Int_t numError;

    *_pipe >> msg >> value >> _evalCarry >> numError;

    if (msg!=ReturnValue) {
      std::cout << "RooRealMPFE::evaluate(" << GetName()
      << ") ERROR: unexpected message from server process: " << msg << std::endl ;
      return 0 ;
    }
    if (_verboseServer) std::cout << "RooRealMPFE::evaluate(" << GetName()
              << ") IPC fromServer> ReturnValue " << value << std::endl ;

    if (_verboseServer) std::cout << "RooRealMPFE::evaluate(" << GetName()
              << ") IPC fromServer> NumErrors " << numError << std::endl ;
    if (numError) {
      // Retrieve remote errors and feed into local error queue
      char *msgbuf1 = nullptr;
      char *msgbuf2 = nullptr;
      char *msgbuf3 = nullptr;
      RooAbsArg *ptr = nullptr;
      while (true) {
   *_pipe >> ptr;
   if (!ptr) break;
   *_pipe >> msgbuf1 >> msgbuf2 >> msgbuf3;
   if (_verboseServer) std::cout << "RooRealMPFE::evaluate(" << GetName()
     << ") IPC fromServer> retrieving error log Arg " << ptr << " Msg " << msgbuf1 << std::endl ;

   logEvalError(reinterpret_cast<RooAbsReal*>(ptr),msgbuf3,msgbuf1,msgbuf2) ;
      }
      std::free(msgbuf1);
      std::free(msgbuf2);
      std::free(msgbuf3);
    }

    // Mark end of calculation in progress
    _calcInProgress = false ;
    return_value = value ;
#endif // _WIN32
  }

  return return_value;
}



////////////////////////////////////////////////////////////////////////////////
/// Terminate remote server process and return front-end class
/// to standby mode. Calls to calculate() or evaluate() after
/// this call will automatically recreated the server process.

void RooRealMPFE::standby()
{
#ifndef _WIN32
  if (_state==Client) {
    if (_pipe->good()) {
      // Terminate server process ;
      if (_verboseServer) std::cout << "RooRealMPFE::standby(" << GetName()
   << ") IPC toServer> Terminate " << std::endl;
      int msg = Terminate;
      *_pipe << msg << BidirMMapPipe::flush;
      // read handshake
      msg = 0;
      *_pipe >> msg;
      if (Terminate != msg || 0 != _pipe->close()) {
   std::cerr << "In " << __func__ << "(" << __FILE__ ", " << __LINE__ <<
     "): Server shutdown failed." << std::endl;
      }
    } else {
      if (_verboseServer) {
   std::cerr << "In " << __func__ << "(" << __FILE__ ", " <<
     __LINE__ << "): Pipe has already shut down, not sending "
     "Terminate to server." << std::endl;
      }
    }
    // Close pipes
    delete _pipe;
    _pipe = nullptr;

    // Revert to initialize state
    _state = Initialize;
  }
#endif // _WIN32
}



////////////////////////////////////////////////////////////////////////////////
/// Intercept call to optimize constant term in test statistics
/// and forward it to object on server side.

void RooRealMPFE::constOptimizeTestStatistic(ConstOpCode opcode, bool doAlsoTracking)
{
#ifndef _WIN32
  if (_state==Client) {

    int msg = ConstOpt ;
    int op = opcode;
    *_pipe << msg << op << doAlsoTracking;
    if (_verboseServer) std::cout << "RooRealMPFE::constOptimize(" << GetName()
              << ") IPC toServer> ConstOpt " << opcode << std::endl ;

    initVars() ;
  }
#endif // _WIN32

  if (_state==Inline) {
    ((RooAbsReal&)_arg.arg()).constOptimizeTestStatistic(opcode,doAlsoTracking) ;
  }
}



////////////////////////////////////////////////////////////////////////////////
/// Control verbose messaging related to inter process communication
/// on both client and server side

void RooRealMPFE::setVerbose(bool clientFlag, bool serverFlag)
{
#ifndef _WIN32
  if (_state==Client) {
    int msg = Verbose ;
    *_pipe << msg << serverFlag;
    if (_verboseServer) std::cout << "RooRealMPFE::setVerbose(" << GetName()
              << ") IPC toServer> Verbose " << (serverFlag?1:0) << std::endl ;
  }
#endif // _WIN32
  _verboseClient = clientFlag ; _verboseServer = serverFlag ;
}


////////////////////////////////////////////////////////////////////////////////
/// Control verbose messaging related to inter process communication
/// on both client and server side

void RooRealMPFE::applyNLLWeightSquared(bool flag)
{
#ifndef _WIN32
  if (_state==Client) {
    int msg = ApplyNLLW2 ;
    *_pipe << msg << flag;
    if (_verboseServer) std::cout << "RooRealMPFE::applyNLLWeightSquared(" << GetName()
              << ") IPC toServer> ApplyNLLW2 " << (flag?1:0) << std::endl ;
  }
#endif // _WIN32
  doApplyNLLW2(flag) ;
}


////////////////////////////////////////////////////////////////////////////////

void RooRealMPFE::doApplyNLLW2(bool flag)
{
  RooNLLVar* nll = dynamic_cast<RooNLLVar*>(_arg.absArg()) ;
  if (nll) {
    nll->applyWeightSquared(flag) ;
  }
}


////////////////////////////////////////////////////////////////////////////////
/// Control verbose messaging related to inter process communication
/// on both client and server side

void RooRealMPFE::enableOffsetting(bool flag)
{
#ifndef _WIN32
  if (_state==Client) {
    int msg = EnableOffset ;
    *_pipe << msg << flag;
    if (_verboseServer) std::cout << "RooRealMPFE::enableOffsetting(" << GetName()
              << ") IPC toServer> EnableOffset " << (flag?1:0) << std::endl ;
  }
#endif // _WIN32
  ((RooAbsReal&)_arg.arg()).enableOffsetting(flag) ;
}



////////////////////////////////////////////////////////////////////////////////
/// Destructor. Terminate all parallel processes still registered with
/// the sentinel

RooMPSentinel::~RooMPSentinel()
{
  for(auto * mpfe : static_range_cast<RooRealMPFE*>(_mpfeSet)) {
    mpfe->standby() ;
  }
}



////////////////////////////////////////////////////////////////////////////////
/// Register given multi-processor front-end object with the sentinel

void RooMPSentinel::add(RooRealMPFE& mpfe)
{
  _mpfeSet.add(mpfe,true) ;
}



////////////////////////////////////////////////////////////////////////////////
/// Remove given multi-processor front-end object from the sentinel

void RooMPSentinel::remove(RooRealMPFE& mpfe)
{
  _mpfeSet.remove(mpfe,true) ;
}

/// \endcond
