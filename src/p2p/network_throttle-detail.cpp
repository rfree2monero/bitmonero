// Copyright (c) 2014, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/* rfree: implementation for throttle details */

#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <atomic>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/interprocess/detail/atomic.hpp>
#include <boost/thread/thread.hpp>

#include <memory>

#include "syncobj.h"

#include "../../contrib/epee/include/net/net_utils_base.h" 
#include "../../contrib/epee/include/misc_log_ex.h" 
#include <boost/lambda/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/chrono.hpp>
#include <boost/utility/value_init.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/thread.hpp> 
#include "misc_language.h"
#include "pragma_comp_defs.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "gnuplot_i.hpp"



#include <boost/asio/basic_socket.hpp>
#include <boost/asio/ip/unicast.hpp>
#include "../../contrib/epee/include/net/abstract_tcp_server2.h"

// TODO:
#include "../../src/p2p/network_throttle-detail.hpp"

#include "../../external/otshell_utils/utils.hpp"

// ################################################################################################
// ################################################################################################
// the "header part". Not separeted out for .hpp because point of this modification is 
// to rebuild just 1 translation unit while working on this code.
// (But maybe common parts will be separated out later though - if needed)
// ################################################################################################
// ################################################################################################

using namespace nOT::nUtils;

namespace epee
{
namespace net_utils
{


/* ============================================================================ */

class connection_basic_pimpl {
	public:
		connection_basic_pimpl(const std::string &name);

		static int m_default_tos;

		network_throttle_bw m_throttle; // per-perr
    critical_section m_throttle_lock;

		void sleep_before_packet(size_t packet_size, int phase); // execute a sleep ; phase is not really used now(?) could be used for different kinds of sleep e.g. direct/queue write
};


} // namespace
} // namespace






// ################################################################################################
// ################################################################################################
// The implementation part
// ################################################################################################
// ################################################################################################

namespace epee
{
namespace net_utils
{

// ================================================================================================
// network_throttle
// ================================================================================================

network_throttle::~network_throttle() { }

network_throttle::packet_info::packet_info() 
	: m_size(0)
{ 
}

network_throttle::network_throttle(const std::string &nameshort, const std::string &name, int window_size)
	: m_window_size( (window_size==-1) ? 10 : window_size  ),
	  m_history( m_window_size ), m_nameshort(nameshort)
{
	set_name(name);
	m_network_add_cost = 128;
	m_network_minimal_segment = 256;
	m_network_max_segment = 1024*1024;
	m_any_packet_yet = false;
	m_slot_size = 1.0; // hard coded in few places
	m_target_speed = 16 * 1024; // other defaults are probably defined in the command-line parsing code when this class is used e.g. as main global throttle
	m_target_MB = 0;

}
void network_throttle::save_history_to_graph() {
       std::vector<int> x, y;
       int counter=0;
       for (std::vector<packet_info>::iterator it = this->m_history.begin() ; it !=this->m_history.end(); ++it) {
               x.push_back(counter);
               size_t i=it->m_size;
               y.push_back((int)i);
               counter++;
       }
       try {
               Gnuplot g1("test");
              g1.set_grid();
               g1.savetops("history_graph");
               g1.set_style("lines").plot_xy(x,y,"test1");
       }
       catch (GnuplotException ge) {
      std::cout << "GNU PLOT PROBLEM: " << ge.what() << std::endl;
			//throw ge;
  }
}


void network_throttle::set_name(const std::string &name) 
{
	m_name = name;
}

void network_throttle::set_target_speed( network_speed_kbps target ) 
{
	m_target_speed = target;
	_note_c("net/"+m_nameshort, "Setting LIMIT: " << target << " kbps");
}

void network_throttle::set_target_kill( network_MB target )
{
	_note_c("net/"+m_nameshort, "Setting KILL: " << target << " MB hard limit");
	m_target_MB = target;
}

			
void network_throttle::tick()
{
	double time_now = get_time_seconds();
	if (!m_any_packet_yet) m_start_time = time_now; // starting now

	network_time_seconds current_sample_time_slot = time_to_slot( time_now ); // T=13.7 --> 13  (for 1-second smallwindow)
	network_time_seconds last_sample_time_slot = time_to_slot( m_last_sample_time );

	// moving to next position, and filling gaps
	// !! during this loop the m_last_sample_time and last_sample_time_slot mean the variable moved in +1
	// TODO optimize when moving few slots at once
	while ( (!m_any_packet_yet) || (last_sample_time_slot < current_sample_time_slot))
	{
		LOG_PRINT_L4("Moving counter buffer by 1 second " << last_sample_time_slot << " < " << current_sample_time_slot << " (last time " << m_last_sample_time<<")");
		// rotate buffer 
		for (size_t i=m_history.size()-1; i>=1; --i) m_history[i] = m_history[i-1];
		m_history[0] = packet_info();
		if (! m_any_packet_yet) 
		{
			m_last_sample_time = time_now;	
		}
		m_last_sample_time += 1;	last_sample_time_slot = time_to_slot( m_last_sample_time ); // increase and recalculate time, time slot
		m_any_packet_yet=true;
	}
	m_last_sample_time = time_now; // the real exact last time
}

void network_throttle::handle_trafic_exact(size_t packet_size) 
{
	_handle_trafic_exact(packet_size, packet_size);
}

void network_throttle::_handle_trafic_exact(size_t packet_size, size_t orginal_size)
{
	double A=0, W=0, D=0, R=0;
	tick();
	// save_history_to_graph() ;
	calculate_times(packet_size, A,W,D,R, false,-1);
	m_history[0].m_size += packet_size;

		std::ostringstream oss; oss << "["; 	for (auto sample: m_history) oss << sample.m_size << " ";	 oss << "]" << std::ends;
		std::string history_str = oss.str();

	_info_c( "net/" + m_nameshort , "Throttle " << m_name << ": packet of ~"<<packet_size<<"b " << " (from "<<orginal_size<<" b)" 
		<< " Speed AVG=" << std::setw(8) <<  ((long int)(A/1024)) <<" / " << " Limit="<< ((long int)(m_target_speed/1024)) <<" KiB/sec "
		<< " " << history_str
		);


	//  (window="<<W<<" s)"); // XXX
}

void network_throttle::handle_trafic_tcp(size_t packet_size)
{
	size_t all_size = packet_size + m_network_add_cost;
	all_size = std::max( m_network_minimal_segment , all_size);
	_handle_trafic_exact( all_size , packet_size );
}

void network_throttle::handle_congestion(double overheat) {
	// TODO
}

network_time_seconds network_throttle::get_sleep_time_after_tick(size_t packet_size) {
	tick();
	return get_sleep_time(packet_size);
}

network_time_seconds network_throttle::get_sleep_time(size_t packet_size) const 
{
	double A=0, W=0, D=0, R=0;
	double Dmax;
	calculate_times(packet_size, A,W,D,R, true, -1);              Dmax=D;
	calculate_times(packet_size, A,W,D,R, true, m_window_size/2); Dmax=std::max(D,Dmax);
	calculate_times(packet_size, A,W,D,R, true, 5);               Dmax=std::max(D,Dmax);
	return D;
}
double network_throttle::get_current_overheat() const {
 	auto now = get_time_seconds();
 	auto diff = now - m_overheat_time;
 	auto overheat = m_overheat - diff;
 	overheat = std::max(m_overheat, 0.);
 	return overheat;
}

void network_throttle::set_overheat(double lag) {
  	m_overheat += lag;
  	m_overheat_time = get_time_seconds();
  	LOG_PRINT_L0("Lag: " << lag << ", overheat: " << m_overheat );
}

void network_throttle::calculate_times(size_t packet_size, double &A, double &W, double &D, double &R, bool dbg, double force_window) const 
{
	const double the_window_size = std::min( (double)m_window_size , 
		((force_window>0) ? force_window : m_window_size) 
	);

	if (!m_any_packet_yet) {
		W=0; A=0; D=0; 
		R=0; // should be overrided by caller
		return ; // no packet yet, I can not decide about sleep time
	}

	network_time_seconds window_len = (the_window_size-1) * m_slot_size ; // -1 since current slot is not finished
	window_len += (m_last_sample_time - time_to_slot(m_last_sample_time));  // add the time for current slot e.g. 13.7-13 = 0.7

	auto time_passed = get_time_seconds() - m_start_time;
	W = std::max( std::min( window_len , time_passed ) , m_slot_size )  ; // window length resulting from size of history but limited by how long ago history was started,
	// also at least slot size (e.g. 1 second) to not be ridiculous
	// window_len e.g. 5.7 because takes into account current slot time

	size_t Epast = 0; // summ of traffic till now
	for (auto sample : m_history) Epast += sample.m_size; 

	const size_t E = Epast;
	const size_t Enow = Epast + packet_size ; // including the data we're about to send now

	const double M = m_target_speed; // max
	const double D1 = (Epast - M*W) / M; // delay - how long to sleep to get back to target speed
	const double D2 = (Enow  - M*W) / M; // delay - how long to sleep to get back to target speed (including current packet)

	auto O = get_current_overheat();
	auto Ouse = O * 0 ; // XXX
	D = (D1*0.75 + D2*0.25) + Ouse; // finall sleep depends on both with/without current packet
	//				update_overheat();
	A = Epast/W; // current avg. speed (for info)

	double Wgood=-1;
	{	// how much data we recommend now to download
		Wgood = the_window_size/2 + 1;
		Wgood = std::min(3., std::max(1., std::min( the_window_size/2., Wgood)));
		if (W > Wgood) { // we have a good enough window to estimate
			R = M*W - E; 
		} else { // now window to estimate, so let's request in this way:
			R = M*1 - E;
		}
	}

	if (dbg) {
		std::ostringstream oss; oss << "["; 	for (auto sample: m_history) oss << sample.m_size << " ";	 oss << "]" << std::ends;
		std::string history_str = oss.str();
		_dbg1_c( "net/"+m_nameshort+"_c" ,
			"dbg " << m_name << ": " 
			<< "speed is A=" << std::setw(8) <<A<<" vs "
			<< "Max=" << std::setw(8) <<M<<" "
			<< " so sleep: "
			<< "D=" << std::setw(8) <<D<<" sec "
			<< "Overheat=" << std::setw(8) <<O<<" sec "
			<< "E="<< std::setw(8) << E << " (Enow="<<std::setw(8)<<Enow<<") "
			<< "M=" << std::setw(8) << M <<" W="<< std::setw(8) << W << " "
			<< "R=" << std::setw(8) << R << " Wgood" << std::setw(8) << Wgood << " "
			<< "History: " << std::setw(8) << history_str << " "
			<< "m_last_sample_time=" << std::setw(8) << m_last_sample_time
		);

	}
}

double network_throttle::get_time_seconds() const {
	using namespace boost::chrono;
	auto point = steady_clock::now();
	auto time_from_epoh = point.time_since_epoch();
	auto ms = duration_cast< milliseconds >( time_from_epoh ).count();
	double ms_f = ms;
	return ms_f / 1000.;
}

size_t network_throttle::get_recommended_size_of_planned_transport_window(double force_window) const {
	double A=0,W=0,D=0,R=0;
	network_throttle::calculate_times(0, A, W, D, R, true, force_window);
	R += m_network_add_cost;
	if (R<0) R=0;
	if (R>m_network_max_segment) R=m_network_max_segment;
	size_t RI = (long int)R;
	return RI;
}

size_t network_throttle::get_recommended_size_of_planned_transport() const {
	size_t Rmin;
	Rmin =           get_recommended_size_of_planned_transport_window( -1 );
	Rmin = std::min( get_recommended_size_of_planned_transport_window( m_window_size/2) , Rmin );
	Rmin = std::min( get_recommended_size_of_planned_transport_window( 8              ) , Rmin );
	return Rmin;
}


} // namespace
} // namespace

