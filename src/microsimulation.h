/**
 * @file microsimulation.cc
 * @author  Mark Clements <mark.clements@ki.se>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION 

 cMessage and cProcess classes, providing some compatability between
 SSIM and the OMNET++ API. This is specialised for use as an R package
 (#includes and REprintf).

 It also provides several utility classes: Means for statistical
 collection and Rpexp for piecewise constant exponential random number
 generation. It also provides a utility function rweibullHR().

*/

#ifndef EVENT_H
#define EVENT_H

#include <R.h>
#include <Rmath.h>
#include <Rdefines.h>
#include <R_ext/Random.h>
#include "ssim.h"
#include "RngStream.h"
#include <string>
#include <vector>

#include <algorithm>
#include <utility>
#include <map>
#include <Rcpp.h>

//using namespace std;
//using namespace ssim;
using std::string;
using std::vector;
using std::map;
using std::pair;
using ssim::Time;
using ssim::Sim;

// should we use the ssim namespace?

#define WithRNG(rng,expr) (rng->set(), expr)

/**
   @brief cMessage class for OMNET++ API compatibility.
   This provides a heavier message class than Sim::Event, with
   short 'kind' and std::string 'name' attributes. 
   The events by default are scheduled using cProcess::scheduleAt(),
   and handled using cProcess::handleMessage() (as per OMNET++).
*/
class cMessage : public ssim::Event {
public:
  short kind;
  string name;
  Time sendingTime, timestamp;
  // this does NOT include schedulePriority
  cMessage(const short k = -1, const string n = "") : kind(k), name(n) {
    sendingTime = Sim::clock();
  }
  // currently no setters (keep it lightweight?)
  short getKind() { return kind; }
  string getName() { return name; }
  Time getTimestamp() { return timestamp; }
  Time getSendingTime() {return sendingTime; }
};

/**
   @brief cProcess class for OMNET++ API compatibility.
   This provides a default for Process::process_event() that calls
   cProcess::handleMessage(). This class also provides scheduleAt()
   methods for insert cMessages into the process event queue.
 */
class cProcess : public ssim::Process {
public:
 cProcess() : previousEventTime(0.0) { }; 
  virtual void handleMessage(const cMessage * msg) = 0;
  virtual void process_event(const ssim::Event * e) { // virtual or not?
    const cMessage * msg;
    if ((msg = dynamic_cast<const cMessage *>(e)) != 0) { // cf. static_cast
      handleMessage(msg);
      previousEventTime = Sim::clock();
    } else {
      // cf. cerr, specialised for R
      REprintf("cProcess is only written to receive cMessage events\n");
    }
  }
  virtual void scheduleAt(Time t, cMessage * msg) { // virtual or not?
    msg->timestamp = t;
    Sim::self_signal_event(msg, t - Sim::clock());  
  }
  virtual void scheduleAt(Time t, string n) {
    scheduleAt(t, new cMessage(-1,n));  
  }
  virtual void scheduleAt(Time t, short k) {
    scheduleAt(t, new cMessage(k));  
  }
  Time previousEventTime;
};


/** 
    Function struct used to compare a message name with a given string
*/
struct cMessageNameEq : public std::binary_function<const ssim::Event *,string,bool> {
  bool operator()(const ssim::Event* e, const string s) const;
};
inline bool cMessageNameEq::operator() (const ssim::Event * e, const string s) const
{ 
  const cMessage * msg = dynamic_cast<const cMessage *>(e);
  return (msg != NULL && msg->name == s); 
};

/** 
    Function struct used to compare a message kind with a given short
*/
struct cMessageKindEq : public std::binary_function<const ssim::Event *,short,bool> {
  bool operator()(const ssim::Event* e, const short k) const;
};
inline bool cMessageKindEq::operator() (const ssim::Event * e, const short k) const
{ 
  const cMessage * msg = dynamic_cast<const cMessage *>(e);
  return (msg != NULL && msg->kind == k); 
};

/**
   Function to remove messages with the given name from the queue (NB: void)
*/
void remove_name(string name);

/**
   Function to remove messages with the given kind from the queue (NB: void)
*/
void remove_kind(short kind);

/**
   simtime_t typedef for OMNET++ API compatibility
*/
typedef Time simtime_t;

/**
   simTime() function for OMNET++ API compatibility
*/
Time simTime();

/**
   now() function for compatibility with C++SIM
*/
Time now();

/**
   @brief Utility class to incrementally add values to calculate the mean,
   sum, variance and standard deviation.
 */
class Means {
public:
  double mean() { return _sum/_n; }
  double var() {return (long double)_n/(_n-1)*(_sumsq/_n - mean()*mean()); }
  int n() {return _n;}
  double sum() {return _sum;}
  double sd() {return sqrt(var()); }
  Means() : _n(0), _sum(0.0), _sumsq(0.0) {}
  Means* operator +=(const double value) {
    _n++;
    _sum += (long double) value;
    _sumsq += (long double)value * (long double)value;
    return this;
  }
  //friend std::ostream& operator<<(std::ostream& os, Means& m);
private:
  int _n;
  long double _sum, _sumsq;
};

/**
   Random number generator class for piecewise constant hazards
 */
class Rpexp {
public: 
  Rpexp(double *hin, double *tin, int nin) : n(nin) {
    int i;
    H.resize(n);
    t.resize(n);
    h.resize(n);
    H[0]=0.0; h[0]=hin[0]; t[0]=tin[0];
    if (n>1) {
      for(i=1;i<n;i++) {
	h[i]=hin[i]; t[i]=tin[i];
	H[i] = H[i-1]+(t[i]-t[i-1])*h[i-1];
      }
    }
  }
  double rand(double from = 0.0) {
    double v = 0.0, H0 = 0.0, tstar = 0.0;
    int i = 0, i0 = 0;
    if (from > 0.0) {
      i0 = (from >= t[n-1]) ? (n-1) : int(lower_bound(t.begin(), t.end(), from) - t.begin())-1;
      H0 = H[i0] + (from - t[i0])*h[i0];
    }
    v = R::rexp(1.0) + H0;
    i = (v >= H[n-1]) ? (n-1) : int(lower_bound(H.begin(), H.end(), v) - H.begin())-1;
    tstar = t[i]+(v-H[i])/h[i];
    return tstar;
  }

 private:
  vector<double> H, h, t;
  int n;
};


/** 
    @brief Random Weibull distribution for a given shape, scale and hazard ratio
*/
double rweibullHR(double shape, double scale, double hr);


/** 
    @brief C++ wrapper class for the RngStreams library. 
    set() selects this random number stream.
    nextSubstream() and nextSubStream() move to the next sub-stream.
    TODO: add other methods.
*/
class Rng {
  public:
    Rng(std::string n = "");
    ~Rng();
    void set();
    void nextSubstream();
    void nextSubStream();
    RngStream stream;
};


extern "C" { // functions that will be called from R

  /** 
      @brief A utility function to create the current_stream.
      Used when initialising the microsimulation package in R.
  */
  void r_create_current_stream();
  
  /** 
      @brief A utility function to remove the current_stream.
      Used when finalising the microsimulation package in R.
  */
  void r_remove_current_stream();
  
  /** 
      @brief Simple test of the random streams (with a stupid name)
  */
  void test_rstream2(double * x);
  
} // extern "C"


  /** 
      @brief Simple function to calculate the integral between the start and end times
      for (1+kappa)^(-u), where kappa is the discountRate (e.g. 0.03)
  */
double discountedInterval(double start, double end, double discountRate);

template< class Tstate, class Tevent, class T >
class EventReport {
public:
  void setPartition(const vector<T> partition) {
    _partition = partition;
    _max = * max_element(_partition.begin(), _partition.end());
  }
  void clear() {
    _pt.clear();
    _events.clear();
    _prev.clear();
    _partition.clear();
  }
  void add(const Tstate state, const Tevent event, const T lhs, const T rhs) {
    typename vector< T >::iterator lo, it;
    T itmax;
    lo = lower_bound (_partition.begin(), _partition.end(), lhs);
    if (lhs<*lo) lo -= 1;
    itmax = rhs<_max ? rhs : _max; // truncates if outside of partition!
    for (it=lo; (*it)<itmax; ++it) {
      _pt[state][*it] += (*(it+1)<rhs ? (*(it+1)) : rhs) - ((*it)<lhs ? lhs : (*it));
      if (lhs<=(*it) & (*it)<rhs) 
	_prev[state][*it] += 1;
    }
    if (rhs<_max)
      _events[state][event][*(it-1)] += 1;
  }

  SEXP out() {

    vector<T> ptAge, ptPt;
    vector<Tstate> ptState;
    typename map<T,T>::iterator it;
    typename map<Tstate, map<T,T> >::iterator it2;
    for (it2=_pt.begin(); it2 != _pt.end(); ++it2) {
      for (it=(*it2).second.begin(); it != (*it2).second.end(); ++it) {
	ptState.push_back((*it2).first);
	ptAge.push_back((*it).first);
	ptPt.push_back((*it).second);
      }
    }
    
    vector<T> evAge;
    vector<Tstate> evState;
    vector<Tevent> evEvent;
    vector<int> evCount;
    typename map<T,int>::iterator itdi1;
    typename map<Tevent, map<T,int> >::iterator itdi2;
    typename map<Tstate, map<Tevent, map<T,int> > >::iterator itdi3;
    for (itdi3=_events.begin(); itdi3 != _events.end(); ++itdi3) {
      for (itdi2=(*itdi3).second.begin(); itdi2 != (*itdi3).second.end(); ++itdi2) {
	for (itdi1=(*itdi2).second.begin(); itdi1 != (*itdi2).second.end(); ++itdi1) {
	  evState.push_back((*itdi3).first);
	  evEvent.push_back((*itdi2).first);
	  evAge.push_back((*itdi1).first);
	  evCount.push_back((*itdi1).second);
	}
      }
    }
    
    typename map<Tstate, map<T,int> >::iterator itdi4;
    vector<Tstate> prState;
    vector<T> prAge;
    vector<int> prCount;
    for (itdi4=_prev.begin(); itdi4 != _prev.end(); ++itdi4) {
      for (itdi1=(*itdi4).second.begin(); itdi1 != (*itdi4).second.end(); ++itdi1) {
	prState.push_back((*itdi4).first);
	prAge.push_back((*itdi1).first);
	prCount.push_back((*itdi1).second);
      }
    }
    
    return Rcpp::List::create(Rcpp::Named("pt") = 
			      Rcpp::DataFrame::create(Rcpp::Named("state") = ptState,
						      Rcpp::Named("age") = ptAge,
						      Rcpp::Named("pt") = ptPt),
			      Rcpp::Named("events") = 
			      Rcpp::DataFrame::create(Rcpp::Named("state") = evState,
						      Rcpp::Named("event") = evEvent,
						      Rcpp::Named("age") = evAge,
						      Rcpp::Named("n") = evCount),
			      Rcpp::Named("prev") = 
			      Rcpp::DataFrame::create(Rcpp::Named("state") = prState,
						      Rcpp::Named("age") = prAge,
						      Rcpp::Named("n") = prCount));
  }
  
  T _max;
  vector<T> _partition;
  map<Tstate, map<T, int> > _prev;
  map<Tstate, map< T, T > > _pt;
  map<Tstate, map<Tevent, map< T, int > > > _events;
};


template< class Tstate1, class Tstate2, class Tevent, class T >
  class EventReportTwoStates {
public:
  void setPartition(const vector<T> partition) {
    _partition = partition;
    _max = * max_element(_partition.begin(), _partition.end());
  }
  void clear() {
    _pt.clear();
    _events.clear();
    _prev.clear();
    _partition.clear();
  }
  void add(const Tstate1 state1, Tstate2 state2, const Tevent event, const T lhs, const T rhs) {
    typename vector< T >::iterator lo, it;
    T itmax;
    pair<Tstate1,Tstate2> state = std::make_pair(state1,state2); // implementation
    lo = lower_bound (_partition.begin(), _partition.end(), lhs);
    if (lhs<*lo) lo -= 1;
    itmax = rhs<_max ? rhs : _max; // truncates if outside of partition!
    for (it=lo; (*it)<itmax; ++it) {
      _pt[state][*it] += (*(it+1)<rhs ? (*(it+1)) : rhs) - ((*it)<lhs ? lhs : (*it));
      if (lhs<=(*it) & (*it)<rhs) 
	_prev[state][*it] += 1;
    }
    if (rhs<_max)
      _events[state][event][*(it-1)] += 1;
  }

  SEXP out() {

    vector<T> ptAge, ptPt;
    vector< Tstate1 > ptState1;
    vector< Tstate2 > ptState2;
    typename map<T,T>::iterator it;
    typename map< pair<Tstate1,Tstate2>, map<T,T> >::iterator it2;
    for (it2=_pt.begin(); it2 != _pt.end(); ++it2) {
      for (it=(*it2).second.begin(); it != (*it2).second.end(); ++it) {
	ptState1.push_back((*it2).first.first);
	ptState2.push_back((*it2).first.second);
	ptAge.push_back((*it).first);
	ptPt.push_back((*it).second);
      }
    }
    
    vector<T> evAge;
    vector<Tstate1> evState1;
    vector<Tstate2> evState2;
    vector<Tevent> evEvent;
    vector<int> evCount;
    typename map<T,int>::iterator itdi1;
    typename map<Tevent, map<T,int> >::iterator itdi2;
    typename map< pair<Tstate1,Tstate2>, map<Tevent, map<T,int> > >::iterator itdi3;
    for (itdi3=_events.begin(); itdi3 != _events.end(); ++itdi3) {
      for (itdi2=(*itdi3).second.begin(); itdi2 != (*itdi3).second.end(); ++itdi2) {
	for (itdi1=(*itdi2).second.begin(); itdi1 != (*itdi2).second.end(); ++itdi1) {
	  evState1.push_back((*itdi3).first.first);
	  evState2.push_back((*itdi3).first.second);
	  evEvent.push_back((*itdi2).first);
	  evAge.push_back((*itdi1).first);
	  evCount.push_back((*itdi1).second);
	}
      }
    }
    
    typename map< pair<Tstate1,Tstate2>, map<T,int> >::iterator itdi4;
    vector< Tstate1 > prState1;
    vector< Tstate2 > prState2;
    vector<T> prAge;
    vector<int> prCount;
    for (itdi4=_prev.begin(); itdi4 != _prev.end(); ++itdi4) {
      for (itdi1=(*itdi4).second.begin(); itdi1 != (*itdi4).second.end(); ++itdi1) {
	prState1.push_back((*itdi4).first.first);
	prState2.push_back((*itdi4).first.second);
	prAge.push_back((*itdi1).first);
	prCount.push_back((*itdi1).second);
      }
    }
    
    return Rcpp::List::create(Rcpp::Named("pt") = 
			      Rcpp::DataFrame::create(Rcpp::Named("state1") = ptState1,
						      Rcpp::Named("state2") = ptState2,
						      Rcpp::Named("age") = ptAge,
						      Rcpp::Named("pt") = ptPt),
			      Rcpp::Named("events") = 
			      Rcpp::DataFrame::create(Rcpp::Named("state1") = evState1,
						      Rcpp::Named("state2") = evState2,
						      Rcpp::Named("event") = evEvent,
						      Rcpp::Named("age") = evAge,
						      Rcpp::Named("n") = evCount),
			      Rcpp::Named("prev") = 
			      Rcpp::DataFrame::create(Rcpp::Named("state1") = prState1,
						      Rcpp::Named("state2") = prState2,
						      Rcpp::Named("age") = prAge,
						      Rcpp::Named("n") = prCount));
  }
  
  T _max;
  vector<T> _partition;
  map< pair<Tstate1,Tstate2>, map<T, int> > _prev;
  map< pair<Tstate1,Tstate2>, map< T, T > > _pt;
  map< pair<Tstate1,Tstate2>, map<Tevent, map< T, int > > > _events;
};


namespace R {
  /**
     @brief rnorm function constrained to be positive. This uses brute-force re-sampling rather
     than conditioning on the distribution function.
  */
  double rnormPos(double mean, double sd);
}

#endif
