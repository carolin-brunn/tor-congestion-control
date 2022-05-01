#ifndef __TOR_H__
#define __TOR_H__

#include "tor-base.h"
#include "cell-header.h"

#include "ns3/uinteger.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"

namespace ns3 {

#define CIRCWINDOW_START 1000
#define CIRCWINDOW_INCREMENT 100
#define STREAMWINDOW_START 500
#define STREAMWINDOW_INCREMENT 50

#define CELL_PAYLOAD_SIZE 498
#define CELL_NETWORK_SIZE 512

struct buf_t
{
  uint32_t size; // How many bytes is this buffer holding right now?
  uint8_t data[CELL_NETWORK_SIZE]; //Left-over chunk, or NULL for none.
};

class Circuit;
class Connection;
class TorApp;


class Circuit : public BaseCircuit
{
public:
  Circuit (uint16_t, Ptr<Connection>, Ptr<Connection>, int, int);
  ~Circuit ();
  void DoDispose ();

  virtual Ptr<Packet> PopCell (CellDirection);
  virtual void PushCell (Ptr<Packet>, CellDirection);
  queue<Ptr<Packet> >* GetQueue (CellDirection);
  uint32_t GetQueueSize (CellDirection);
  uint32_t GetQueueSizeBytes (CellDirection);
  uint32_t SendCell (CellDirection);

  Ptr<Connection> GetConnection (CellDirection);
  Ptr<Connection> GetOppositeConnection (CellDirection);
  Ptr<Connection> GetOppositeConnection (Ptr<Connection>);
  CellDirection GetDirection (Ptr<Connection>);
  CellDirection GetOppositeDirection (Ptr<Connection>);

  Ptr<Circuit> GetNextCirc (Ptr<Connection>);
  void SetNextCirc (Ptr<Connection>, Ptr<Circuit>);

  uint32_t GetPackageWindow ();
  uint32_t GetCwnd ();
   uint32_t GetInflight ();
  void IncPackageWindow ();
  uint32_t GetDeliverWindow ();
  void IncDeliverWindow ();

  /*********************************************/
  /*
   * NEW variables for NEW congestion control!
   */
  string m_cong_flavor;
  string m_bdp_flavor;

  void UpdateCwnd (double, Ptr<Connection>);
  void UpdateCwnd_westwood(double, Ptr<Connection>);
  void UpdateCwnd_vegas(double, Ptr<Connection>);
  void UpdateCwnd_nola (double, Ptr<Connection>);

  double CalculateBDP (Ptr<Connection>, CellDirection);
  double CalculateBDP_sendme ();
  double CalculateBDP_cwnd ();
  double CalculateBDP_inflight (CellDirection);

  void CalculateRtt (Ptr<Connection>); //map<uint16_t,Time>, map<uint16_t,Time>, int);
  double CalcEWMASmoothingRTT (int, int);
  double CalcEWMASmoothingBDP (int, int);

  
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("Circuit")
      .SetParent (BaseCircuit::GetTypeId())
      .AddTraceSource ("PackageWindow",
                       "The vanilla Tor package window.",
                       MakeTraceSourceAccessor(&Circuit::package_window),
                       "ns3::TracedValueCallback::int32")
      .AddTraceSource ("DeliverWindow",
                       "The vanilla Tor deliver window.",
                       MakeTraceSourceAccessor(&Circuit::deliver_window),
                       "ns3::TracedValueCallback::int32")
      .AddAttribute ("CongestionFlavor", 
                    "Congestion algortihm used to update cc_window",
                    StringValue ("nola"), //nola/westwood/vegas
                    MakeStringAccessor (&Circuit::m_cong_flavor), MakeStringChecker ())
      .AddAttribute ("BDPFlavor", 
                    "BDP algortihm used to estimate BDP",
                    StringValue (string ("piecewise")), //sendme/cwnd/inflight
                    MakeStringAccessor (&Circuit::m_bdp_flavor), MakeStringChecker ())
      .AddAttribute ("oldCongControl", "Choose old or new congestion control",
                      IntegerValue (int(0)),
                      MakeIntegerAccessor (&Circuit::oldCongControl), 
                      MakeIntegerChecker<int> ());
      ;
    return tid;
  }
  // TODO: why is AddAttribute done in .h and not .cc like for torBaseApp?

protected:
  Ptr<Packet> PopQueue (queue<Ptr<Packet> >*);
  bool IsSendme (Ptr<Packet>);
  Ptr<Packet> CreateSendme ();

  queue<Ptr<Packet> > *p_cellQ;
  queue<Ptr<Packet> > *n_cellQ;

  //Next circuit in the doubly-linked ring of circuits waiting to add cells to {n,p}_conn.
  Ptr<Circuit> next_active_on_n_conn;
  Ptr<Circuit> next_active_on_p_conn;

  Ptr<Connection> p_conn;   /* The OR connection that is previous in this circuit. */
  Ptr<Connection> n_conn;   /* The OR connection that is next in this circuit. */

  /** How many relay data cells can we package (read from edge streams)
   * on this circuit before we receive a circuit-level sendme cell asking
   * for more? */
  TracedValue<int32_t> package_window;
  /** How many relay data cells will we deliver (write to edge streams)
   * on this circuit? When deliver_window gets low, we send some
   * circuit-level sendme cells to indicate that we're willing to accept
   * more. */
  TracedValue<int32_t> deliver_window;

  int m_windowStart;
  int m_windowIncrement;

  /*********************************************/
  /*
   * NEW variables for NEW congestion control
   */
  int oldCongControl; // TODO: bool?? NEW indicate whether old or new congestion control shall be used

  // maps to handle RTT etc
  map<uint16_t,Time> sentPck_timestamps; // NEW map to save "sent" timestamp for every 50th packet
  map<uint16_t,Time> ack_timestamps; // NEW map to save timestamps of received ACKs (corresponding to 50th packets)

  vector<double> m_raw_rtt; // NEW vector<Time> m_raw_rtt;
  vector<double> m_ewma_rtt; // NEW vactor for N-EWMA smoothed values
  vector<double> m_sendme_bdp_list; // NEW save sendme bdp values for EWMA smoothing

  int cwnd; // NEW congestion window used instead of package window
  int cc_cwnd_min; // NEW minimum circwindow
  int cc_cwnd_max; // NEW maximum circwindow
  int cc_cwnd_init; // NEW initial value for cwnd
  int cc_sendme_inc; // NEW Specifies how many cells a SENDME acks
  int cc_cwnd_inc; // NEW initial congestion window increment
  int cc_cwnd_inc_rate; // NEW How often we update our congestion window, per cwnd worth of packets
              // kept at 1 in Tor!
  int update_cnt; // NEW counter to update only at cc_cwnd_inc_rate

  int cc_bwe_min; // NEW The minimum number of SENDME acks to average over in order to estimate bandwidth
  int cc_ewma_cwnd_pct; //NEW specifies percent of congestion windows worth of SENDME acks for smoothing
  int cc_ewma_max; // NEW max no of acks used for congestion window
  
  uint64_t m_pckCounter_sent; // NEW count how many packets have been sent in total
  uint64_t m_pckCounter_recv; // NEW count how many packets have been received in total
  int m_inflight; // NEW count how many packets have not been acknowledged yet
  int m_num_sendmes; // NEW count how many sendmes have been received
  double m_num_sendme_timestamp_delta; // NEW add up the deltas between sendmes
  
  double m_min_rtt; //Time m_min_rtt; // NEW keep track of minimum RTT
  double m_max_rtt; //Time m_max_rtt; // NEW keep track of maximum RTT
  double m_curr_rtt; //NEW current smoothed ewma RTT value
  
  // NOLA
  int cc_nola_overshoot; // NEW

  // WESTWOOD
  int cc_westwood_rtt_thresh; // NEW define cutoff threshold to deliver congestion signal
  bool cc_westwood_min_backoff; // NEW 1 => take the min of BDP estimate and westwood backoff; 0 => take the max of BDP estimate and westwood backoff.
  double cc_westwood_cwnd_m; // NEW Specifies how much to reduce the congestion window after a congestion signal, as a fraction of 100.
        // => change to fraction between 0 and 1. otherwise it would not reduce
        // fractino of 100 probably only to avoid double values

  double cc_westwood_rtt_m; // NEW Specifies a backoff percent of RTT_max, upon receipt of a congestion signal.
  
  // VEGAS
  // NEW These parameters govern the number of cells that [TOR_VEGAS] can detect in queue before reacting.
  int cc_vegas_alpha;
  int cc_vegas_beta;
  int cc_vegas_gamma;
  int cc_vegas_delta;

  // WESTWOOD & VEGAS
  bool in_slow_start; // NEW keep track whether programm is still in slow start
  double cc_cwnd_inc_pct_ss; // NEW Percentage of the current congestion window to increment by during slow start, every cwnd 
  int next_cc_event;
};




class Connection : public SimpleRefCount<Connection>
{
public:
  Connection (TorApp*, Ipv4Address, int);
  ~Connection ();

  Ptr<Circuit> GetActiveCircuits ();
  void SetActiveCircuits (Ptr<Circuit>);
  uint8_t GetType ();
  bool SpeaksCells ();
  uint32_t Read (vector<Ptr<Packet> >*, uint32_t);
  uint32_t Write (uint32_t);
  void ScheduleWrite (Time = Seconds (0));
  void ScheduleRead (Time = Seconds (0));
  bool IsBlocked ();
  void SetBlocked (bool);
  Ptr<Socket> GetSocket ();
  void SetSocket (Ptr<Socket>);
  Ipv4Address GetRemote ();
  uint32_t GetOutbufSize ();
  uint32_t GetInbufSize ();

  static void RememberName (Ipv4Address, string);
  string GetRemoteName ();
  TorApp * GetTorApp() { return torapp; }

  // index within each circuit's data stream that was last sent
  map<int,uint64_t> data_index_last_seen;
  map<int,uint64_t> data_index_last_delivered;

  void CountFinalReception(int circid, uint32_t length);

private:
  TorApp* torapp;
  Ipv4Address remote;
  Ptr<Socket> m_socket;

  buf_t inbuf; /**< Buffer holding left over data read over this connection. */
  buf_t outbuf; /**< Buffer holding left over data to write over this connection. */

  uint8_t m_conntype;
  bool reading_blocked;

  // Linked ring of circuits
  Ptr<Circuit> active_circuits;

  EventId read_event;
  EventId write_event;

  static map<Ipv4Address, string> remote_names;
};





class TorApp : public TorBaseApp
{
public:
  static TypeId GetTypeId (void);
  TorApp ();
  virtual ~TorApp ();
  virtual void AddCircuit (int, Ipv4Address, int, Ipv4Address, int,
                           Ptr<PseudoClientSocket> clientSocket = 0);

  virtual void StartApplication (void);
  virtual void StopApplication (void);

  Ptr<Circuit> GetCircuit (uint16_t);

  virtual Ptr<Connection> AddConnection (Ipv4Address, int);
  void AddActiveCircuit (Ptr<Connection>, Ptr<Circuit>);

// private:
  void HandleAccept (Ptr<Socket>, const Address& from);

  virtual void ConnReadCallback (Ptr<Socket>);
  virtual void ConnWriteCallback (Ptr<Socket>, uint32_t);
  void PackageRelayCell (Ptr<Connection> conn, Ptr<Packet> data);
  void PackageRelayCellImpl (uint16_t, Ptr<Packet>);
  void ReceiveRelayCell (Ptr<Connection> conn, Ptr<Packet> cell);
  void AppendCellToCircuitQueue (Ptr<Circuit> circ, Ptr<Packet> cell, CellDirection direction);
  Ptr<Circuit> LookupCircuitFromCell (Ptr<Packet>);
  void RefillReadCallback (int64_t);
  void RefillWriteCallback (int64_t);
  void GlobalBucketsDecrement (uint32_t num_read, uint32_t num_written);
  uint32_t RoundRobin (int base, int64_t bucket);
  Ptr<Connection> LookupConn (Ptr<Socket>);

  Ptr<Socket> listen_socket;
  vector<Ptr<Connection> > connections;
  map<uint16_t,Ptr<Circuit> > circuits;
  
  int m_windowStart;
  int m_windowIncrement;
  

  // Remember which connection is the next to read from (or write to).
  // Previous operations did not continue because the token bucket ran empty.
  Ptr<Connection> m_scheduleReadHead;
  Ptr<Connection> m_scheduleWriteHead;

  // Callback to trigger after a new socket is established
  TracedCallback<Ptr<TorBaseApp>, // this app
                 CellDirection,   // the direction of the new socket
                 Ptr<Socket>      // the new socket itself
                 > m_triggerNewSocket;
  typedef void (* TorNewSocketCallback) (Ptr<TorBaseApp>, CellDirection, Ptr<Socket>);

  // Callback to trigger after a new pseudo server socket is added
  TracedCallback<Ptr<TorBaseApp>, // this app
                 int,              // circuit id
                 Ptr<PseudoServerSocket>      // the new pseudo socket itself
                 > m_triggerNewPseudoServerSocket;
  typedef void (* TorNewPseudoServerSocketCallback) (Ptr<TorBaseApp>, int, Ptr<PseudoServerSocket>);

  // Callback to trigger when a cell has been sent into the network by an exit
  TracedCallback<Ptr<TorBaseApp>, // this app
                 int,             // circuit id
                 uint64_t,        // byte index in circuit (start)
                 uint64_t         // byte index in circuit (end)
                 > m_triggerBytesEnteredNetwork;
  typedef void (* TorBytesEnteredNetworkCallback) (Ptr<TorBaseApp>, int, uint64_t, uint64_t);

  // Callback to trigger when a cell has been received from the network by an entry
  TracedCallback<Ptr<TorBaseApp>, // this app
                 int,             // circuit id
                 uint64_t,        // byte index in circuit (start)
                 uint64_t         // byte index in circuit (end)
                 > m_triggerBytesLeftNetwork;
  typedef void (* TorBytesLeftNetworkCallback) (Ptr<TorBaseApp>, int, uint64_t, uint64_t);

protected:
  virtual void DoDispose (void);

};


} //namespace ns3

#endif /* __TOR_H__ */
