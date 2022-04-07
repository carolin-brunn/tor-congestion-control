
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"
#include <math.h> 

#include "tor.h"

using namespace std;

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TorApp");
NS_OBJECT_ENSURE_REGISTERED (TorApp);
NS_OBJECT_ENSURE_REGISTERED (Circuit);

TypeId
TorApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TorApp")
    .SetParent<TorBaseApp> ()
    .AddConstructor<TorApp> ()
    .AddAttribute ("WindowStart", "End-to-end sliding window size (in cells).",
                   IntegerValue (1000),
                   MakeIntegerAccessor (&TorApp::m_windowStart),
                   MakeIntegerChecker<int> ())
    .AddAttribute ("WindowIncrement", "End-to-end sliding window increment (in cells).",
                   IntegerValue (100),
                   MakeIntegerAccessor (&TorApp::m_windowIncrement),
                   MakeIntegerChecker<int> ())
    .AddTraceSource ("NewSocket",
                     "Trace indicating that a new socket has been installed.",
                     MakeTraceSourceAccessor (&TorApp::m_triggerNewSocket),
                     "ns3::TorApp::TorNewSocketCallback")
    .AddTraceSource ("NewServerSocket",
                     "Trace indicating that a new pseudo server socket has been installed.",
                     MakeTraceSourceAccessor (&TorApp::m_triggerNewPseudoServerSocket),
                     "ns3::TorApp::TorNewPseudoServerSocketCallback")
    .AddTraceSource ("BytesEnteredNetwork",
                     "Trace indicating that an exit sent a byte of data, identified by its index, into the network.",
                     MakeTraceSourceAccessor (&TorApp::m_triggerBytesEnteredNetwork),
                     "ns3::TorPredApp::TorBytesEnteredNetworkCallback")
    .AddTraceSource ("BytesLeftNetwork",
                     "Trace indicating that an eentry received a byte of data, identified by its index, from the network.",
                     MakeTraceSourceAccessor (&TorApp::m_triggerBytesLeftNetwork),
                     "ns3::TorPredApp::TorBytesLeftNetworkCallback");
  return tid;
}

TorApp::TorApp (void)
{
  listen_socket = 0;
  m_scheduleReadHead = 0;
  m_scheduleWriteHead = 0;
}

TorApp::~TorApp (void)
{
  NS_LOG_FUNCTION (this);
}

void
TorApp::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  listen_socket = 0;

  map<uint16_t,Ptr<Circuit> >::iterator i;
  for (i = circuits.begin (); i != circuits.end (); ++i)
    {
      i->second->DoDispose ();
    }
  circuits.clear ();
  baseCircuits.clear ();
  connections.clear ();
  Application::DoDispose ();
}


/* called by tor-star-helper-cc */
void
TorApp::AddCircuit (int id, Ipv4Address n_ip, int n_conntype, Ipv4Address p_ip, int p_conntype,
                    Ptr<PseudoClientSocket> clientSocket)
{
  TorBaseApp::AddCircuit (id, n_ip, n_conntype, p_ip, p_conntype); // ensures valid connection types

  // ensure unique id
  NS_ASSERT (circuits[id] == 0);

  // allocate and init new circuit
  // get connection with corresponding ip address 
  // or create new connection for node with this address if none exists until then
  Ptr<Connection> p_conn = AddConnection (p_ip, p_conntype); // add a connection to the list of managed connections 
  Ptr<Connection> n_conn = AddConnection (n_ip, n_conntype);

  // set client socket of the predecessor connection, if one was given (default is 0)
  p_conn->SetSocket (clientSocket); // set socket in TorApp (on every node?!)
  m_triggerNewSocket(this, INBOUND, clientSocket); // Callback to trigger after a new socket is established

  // set m_windowStart, m_windowIncrement
  Ptr<Circuit> circ = CreateObject<Circuit> (id, n_conn, p_conn, m_windowStart, m_windowIncrement);

  // add to circuit list maintained by every connection, add circ to linked ring of active circuits
  AddActiveCircuit (p_conn, circ);
  AddActiveCircuit (n_conn, circ);

  // add to the global list of circuits
  circuits[id] = circ;
  baseCircuits[id] = circ;
}

Ptr<Connection>
TorApp::AddConnection (Ipv4Address ip, int conntype)
{
  // find existing or create new connection
  Ptr<Connection> conn;
  vector<Ptr<Connection> >::iterator it;
  for (it = connections.begin (); it != connections.end (); ++it)
    {
      if ((*it)->GetRemote () == ip)
        {
          conn = *it;
          break;
        }
    }

  if (!conn)
    {
      conn = Create<Connection> (this, ip, conntype); // add TorApp, ip and conntype to connection
      connections.push_back (conn);
    }

  return conn;
}

void
TorApp::AddActiveCircuit (Ptr<Connection> conn, Ptr<Circuit> circ)
{
  NS_ASSERT (conn);
  NS_ASSERT (circ);
  if (conn)
    {
      if (!conn->GetActiveCircuits ()) // no active circuits yet
        {
          conn->SetActiveCircuits (circ); // add circ to linked ring of active circuits ( attributes of connection object, e.g. next_active_on_n_conn = circ)
          circ->SetNextCirc (conn, circ);
        }
      else
        {
          Ptr<Circuit> temp = conn->GetActiveCircuits ()->GetNextCirc (conn); // next circuit of avtive circuit = current circ
          circ->SetNextCirc (conn, temp); // set next of current circ
          conn->GetActiveCircuits ()->SetNextCirc (conn, circ); // set next of last entry in current conn
        }
    }
}

/*
is called at the beginning (about 3 times) until circuit is established
*/
void
TorApp::StartApplication (void)
{
  TorBaseApp::StartApplication ();
  m_readbucket.SetRefilledCallback (MakeCallback (&TorApp::RefillReadCallback, this));
  m_writebucket.SetRefilledCallback (MakeCallback (&TorApp::RefillWriteCallback, this));

  // create listen socket
  if (!listen_socket)
    {
      listen_socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
      listen_socket->Bind (m_local);
      listen_socket->Listen ();
      Connection::RememberName(Ipv4Address::ConvertFrom (m_ip), GetNodeName());
    }

  listen_socket->SetAcceptCallback (MakeNullCallback<bool,Ptr<Socket>,const Address &> (),
                                    MakeCallback (&TorApp::HandleAccept,this));

  Ipv4Mask ipmask = Ipv4Mask ("255.0.0.0");

  // iterate over all neighbouring connections
  vector<Ptr<Connection> >::iterator it;
  for ( it = connections.begin (); it != connections.end (); it++ )
    {
      Ptr<Connection> conn = *it;
      NS_ASSERT (conn);

      // if m_ip smaller then connect to remote node
      if (m_ip < conn->GetRemote () && conn->SpeaksCells ())
        {
        	//cout << "Connection to relay edge\n";
          Ptr<Socket> socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
          socket->Bind ();
          socket->Connect (Address (InetSocketAddress (conn->GetRemote (), InetSocketAddress::ConvertFrom (m_local).GetPort ())));
          socket->SetSendCallback (MakeCallback(&TorApp::ConnWriteCallback, this));
          // socket->SetDataSentCallback (MakeCallback (&TorApp::ConnWriteCallback, this));
          socket->SetRecvCallback (MakeCallback (&TorApp::ConnReadCallback, this));
          conn->SetSocket (socket);
          m_triggerNewSocket(this, OUTBOUND, socket);
        }

      if (ipmask.IsMatch (conn->GetRemote (), Ipv4Address ("127.0.0.1")) )
        {
          if (conn->GetType () == SERVEREDGE)
            {
              Ptr<Socket> socket = CreateObject<PseudoServerSocket> ();
              // socket->SetDataSentCallback (MakeCallback (&TorApp::ConnWriteCallback, this));
              socket->SetSendCallback(MakeCallback(&TorApp::ConnWriteCallback, this));
              socket->SetRecvCallback (MakeCallback (&TorApp::ConnReadCallback, this));
              conn->SetSocket (socket);
              m_triggerNewSocket(this, OUTBOUND, socket);

              int circId = conn->GetActiveCircuits () ->GetId ();
              m_triggerNewPseudoServerSocket(this, circId, DynamicCast<PseudoServerSocket>(socket));
            }

          if (conn->GetType () == PROXYEDGE)
            {
              Ptr<Socket> socket = conn->GetSocket ();
              // Create a default pseudo client if none was previously provided via AddCircuit().
              // Normally, this should not happen (a specific pseudo socket should always be
              // provided for a proxy connection).
              if (!socket)
                {
                  socket = CreateObject<PseudoClientSocket> ();
                  m_triggerNewSocket(this, INBOUND, socket);
                }

              // socket->SetDataSentCallback (MakeCallback (&TorApp::ConnWriteCallback, this));
              socket->SetSendCallback(MakeCallback(&TorApp::ConnWriteCallback, this));
              socket->SetRecvCallback (MakeCallback (&TorApp::ConnReadCallback, this));
              conn->SetSocket (socket);   
            }
        }
        
    }
  m_triggerAppStart (Ptr<TorBaseApp>(this));
  NS_LOG_INFO ("StartApplication " << m_name << " ip=" << m_ip);
}

void
TorApp::StopApplication (void)
{
  // close listen socket
  if (listen_socket)
    {
      listen_socket->Close ();
      listen_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }

  // close all connections
  vector<Ptr<Connection> >::iterator it_conn;
  for ( it_conn = connections.begin (); it_conn != connections.end (); ++it_conn )
    {
      Ptr<Connection> conn = *it_conn;
      NS_ASSERT (conn);
      if (conn->GetSocket ())
        {
          conn->GetSocket ()->Close ();
          conn->GetSocket ()->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
          conn->GetSocket ()->SetDataSentCallback (MakeNullCallback<void, Ptr<Socket>, uint32_t > ());
          // conn->GetSocket()->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t > ());
        }
    }
}

Ptr<Circuit>
TorApp::GetCircuit (uint16_t circid)
{
  return circuits[circid];
}


void
TorApp::ConnReadCallback (Ptr<Socket> socket)
{
  // At one of the connections, data is ready to be read. This does not mean
  // the data has been read already, but it has arrived in the RX buffer. We
  // decide whether we want to read it or not (depending on whether the
  // connection is blocked).
  NS_ASSERT (socket);
  Ptr<Connection> conn = LookupConn (socket);
  NS_ASSERT (conn);

  if (conn->IsBlocked ())
    {
      NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Reading blocked, return");
      return;
    }

  uint32_t base = conn->SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t max_read = RoundRobin (base, m_readbucket.GetSize ());

  // find the minimum amount of data to read safely from the socket
  max_read = min (max_read, socket->GetRxAvailable ());
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Reading " << max_read << "/" << socket->GetRxAvailable () << " bytes");

  if (m_readbucket.GetSize() <= 0 && m_scheduleReadHead == 0)
    {
      m_scheduleReadHead = conn;
    }

  if (max_read <= 0)
    {
      return;
    }

  if (!conn->SpeaksCells ())
    {
      max_read = min (conn->GetActiveCircuits ()->GetPackageWindow () * base,max_read);
    }

  vector<Ptr<Packet> > packet_list;
  uint32_t read_bytes = conn->Read (&packet_list, max_read);

  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Got " << packet_list.size () << " packets (read " << read_bytes << " bytes)");

  for (uint32_t i = 0; i < packet_list.size (); i++)
    {
      if (conn->SpeaksCells ())
        {
          NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Handling relay cell...");
          ReceiveRelayCell (conn, packet_list[i]);
        }
      else
        {
          NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] Handling non-relay cell, packaging it...");
          PackageRelayCell (conn, packet_list[i]);
        }
    }

  if (read_bytes > 0)
    {
      // decrement buckets
      GlobalBucketsDecrement (read_bytes, 0);

      // try to read more
      if (socket->GetRxAvailable () > 0)
        {
          // add some virtual processing time before reading more
          Time delay = Time::FromInteger (read_bytes * 2, Time::NS);
          conn->ScheduleRead (delay);
        }
    }
}

void
TorApp::PackageRelayCell (Ptr<Connection> conn, Ptr<Packet> cell)
{
  NS_ASSERT (conn);
  NS_ASSERT (cell);
  Ptr<Circuit> circ = conn->GetActiveCircuits (); 
  NS_ASSERT (circ);

  PackageRelayCellImpl (circ->GetId (), cell);

  CellDirection direction = circ->GetOppositeDirection (conn);
  AppendCellToCircuitQueue (circ, cell, direction);

  //cout << "[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Appended newly packaged cell to circ queue.\n";
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Appended newly packaged cell to circ queue.");
  if (circ->GetPackageWindow () <= 0)
    {
      //cout << "[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Package window empty now. Block reading from " << conn->GetRemote() << "\n";
      NS_LOG_LOGIC ("[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Package window empty now. Block reading from " << conn->GetRemote());
      conn->SetBlocked (true);
      // TODO blocking the whole connection if SENDME window for one of its
      //      circuits is empty
      //  ==> server only!!
    }

  else if ((circ->GetCwnd() - circ->GetInflight()) <= 0)
    {
      //cout << "[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Package window empty now. Block reading from " << conn->GetRemote() << "\n";
      NS_LOG_LOGIC ("[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Package window empty now. Block reading from " << conn->GetRemote());
      conn->SetBlocked (true);
      // TODO blocking the whole connection if SENDME window for one of its
      //      circuits is empty
      //  ==> server only!!
    }

  
}

void
TorApp::PackageRelayCellImpl (uint16_t circ_id, Ptr<Packet> cell)
{
  NS_ASSERT (cell);
  CellHeader h;
  h.SetCircId (circ_id);
  h.SetCmd (RELAY_DATA);
  h.SetType (RELAY);
  h.SetLength (cell->GetSize ());
  cell->AddHeader (h);
}

void
TorApp::ReceiveRelayCell (Ptr<Connection> conn, Ptr<Packet> cell)
{
  NS_ASSERT (conn);
  NS_ASSERT (cell);
  Ptr<Circuit> circ = LookupCircuitFromCell (cell);
  NS_ASSERT (circ);
  //cout << "[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] received relay cell.\n";
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] received relay cell.");
  //NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << Ipv4Address::ConvertFrom (conn->GetRemote()) << "/" << conn->GetRemoteName () << "] received relay cell.");

  // find target connection for relaying
  CellDirection direction = circ->GetOppositeDirection (conn);
  Ptr<Connection> target_conn = circ->GetConnection (direction);
  NS_ASSERT (target_conn);
  target_conn->CountFinalReception(circ->GetId(), cell->GetSize());

  AppendCellToCircuitQueue (circ, cell, direction);
}


Ptr<Circuit>
TorApp::LookupCircuitFromCell (Ptr<Packet> cell)
{
  NS_ASSERT (cell);
  CellHeader h;
  cell->PeekHeader (h);
  return circuits[h.GetCircId ()];
}


/* Add cell to the queue of circ writing to orconn transmitting in direction. */
void
TorApp::AppendCellToCircuitQueue (Ptr<Circuit> circ, Ptr<Packet> cell, CellDirection direction)
{
  NS_ASSERT (circ);
  NS_ASSERT (cell);
  queue<Ptr<Packet> > *queue = circ->GetQueue (direction);
  Ptr<Connection> conn = circ->GetConnection (direction);
  NS_ASSERT (queue);
  NS_ASSERT (conn);

  circ->PushCell (cell, direction);

  //cout << "[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Appended cell. Queue holds " << queue->size () << " cells.\n";
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Circuit " << circ->GetId () << "] Appended cell. Queue holds " << queue->size () << " cells.");
  conn->ScheduleWrite ();
  //cout << "schduled write\n";
}


void
TorApp::ConnWriteCallback (Ptr<Socket> socket, uint32_t tx)
{
  //cout << "Conn WriteCallback was triggered! \n";
  NS_ASSERT (socket);
  Ptr<Connection> conn = LookupConn (socket);
  NS_ASSERT (conn);

  uint32_t newtx = socket->GetTxAvailable ();

  int written_bytes = 0;
  uint32_t base = conn->SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t max_write = RoundRobin (base, m_writebucket.GetSize ());
  max_write = max_write > newtx ? newtx : max_write;

  //cout << "[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] writing at most " << max_write << " bytes into Conn\n";
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] writing at most " << max_write << " bytes into Conn");

  if (m_writebucket.GetSize() <= 0 && m_scheduleWriteHead == 0) {
    m_scheduleWriteHead = conn;
  }

  if (max_write <= 0)
    {
      return;
    }

  written_bytes = conn->Write (max_write);
  NS_LOG_LOGIC ("[" << GetNodeName() << ": Connection " << conn->GetRemoteName () << "] " << written_bytes << " bytes written");

  if (written_bytes > 0)
    {
      GlobalBucketsDecrement (0, written_bytes);

      /* try flushing more */
      conn->ScheduleWrite ();
    }
}



void
TorApp::HandleAccept (Ptr<Socket> s, const Address& from)
{
	// is triggered 3 times in the beginning after the first ReadCallback and WriteCallback
  Ptr<Connection> conn;
  Ipv4Address ip = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
  vector<Ptr<Connection> >::iterator it;
  for (it = connections.begin (); it != connections.end (); ++it)
    {
      if ((*it)->GetRemote () == ip && !(*it)->GetSocket () ) // GetRemote = ip address 
        {
          conn = *it;
          break;
        }
    }

  NS_ASSERT (conn);
  conn->SetSocket (s);
  m_triggerNewSocket(this, INBOUND, s);

  s->SetRecvCallback (MakeCallback (&TorApp::ConnReadCallback, this));
  s->SetSendCallback (MakeCallback(&TorApp::ConnWriteCallback, this));
  // s->SetDataSentCallback (MakeCallback (&TorApp::ConnWriteCallback, this));
  conn->ScheduleWrite();
  conn->ScheduleRead();
}



Ptr<Connection>
TorApp::LookupConn (Ptr<Socket> socket)
{
  vector<Ptr<Connection> >::iterator it;
  for ( it = connections.begin (); it != connections.end (); it++ )
    {
      NS_ASSERT (*it);
      if ((*it)->GetSocket () == socket)
        {
          return (*it);
        }
    }
  return NULL;
}


void
TorApp::RefillReadCallback (int64_t prev_read_bucket)
{
	NS_LOG_LOGIC ("read bucket was " << prev_read_bucket << ". Now " << m_readbucket.GetSize ());
  if (prev_read_bucket <= 0 && m_readbucket.GetSize () > 0)
    {
      vector<Ptr<Connection> >::iterator it;
      vector<Ptr<Connection> >::iterator headit;

      headit = connections.begin();
      if (m_scheduleReadHead != 0) {
        headit = find(connections.begin(),connections.end(),m_scheduleReadHead);
        m_scheduleReadHead = 0;
      }

      it = headit;
      do {
        Ptr<Connection> conn = *it;
        NS_ASSERT(conn);
        conn->ScheduleRead (Time ("10ns"));
        if (++it == connections.end ()) {
          it = connections.begin ();
        }
      } while (it != headit);
    }
}

void
TorApp::RefillWriteCallback (int64_t prev_write_bucket)
{
  NS_LOG_LOGIC ("write bucket was " << prev_write_bucket << ". Now " << m_writebucket.GetSize ());

  if (prev_write_bucket <= 0 && m_writebucket.GetSize () > 0)
    {
      vector<Ptr<Connection> >::iterator it;
      vector<Ptr<Connection> >::iterator headit;

      headit = connections.begin();
      if (m_scheduleWriteHead != 0) {
        headit = find(connections.begin(),connections.end(),m_scheduleWriteHead);
        m_scheduleWriteHead = 0;
      }

      it = headit;
      do {
        Ptr<Connection> conn = *it;
        NS_ASSERT(conn);
        conn->ScheduleWrite ();
        if (++it == connections.end ()) {
          it = connections.begin ();
        }
      } while (it != headit);
    }
}


/** We just read num_read and wrote num_written bytes
 * onto conn. Decrement buckets appropriately. */
void
TorApp::GlobalBucketsDecrement (uint32_t num_read, uint32_t num_written)
{
  m_readbucket.Decrement (num_read);
  m_writebucket.Decrement (num_written);
}



/** Helper function to decide how many bytes out of global_bucket
 * we're willing to use for this transaction. Yes, this is how Tor
 * implements it; no kidding. */
uint32_t
TorApp::RoundRobin (int base, int64_t global_bucket)
{
  uint32_t num_bytes_high = 32 * base;
  uint32_t num_bytes_low = 4 * base;
  int64_t at_most = global_bucket / 8;
  at_most -= (at_most % base);

  if (at_most > num_bytes_high)
    {
      at_most = num_bytes_high;
    }
  else if (at_most < num_bytes_low)
    {
      at_most = num_bytes_low;
    }

  if (at_most > global_bucket)
    {
      at_most = global_bucket;
    }

  if (at_most < 0)
    {
      return 0;
    }
  return at_most;
}


// class Circuit is defined in tor.h
Circuit::Circuit (uint16_t circ_id, Ptr<Connection> n_conn, Ptr<Connection> p_conn,
                  int windowStart, int windowIncrement) : BaseCircuit (circ_id)
{
  this->p_cellQ = new queue<Ptr<Packet> >;
  this->n_cellQ = new queue<Ptr<Packet> >;

  m_windowStart = windowStart;
  m_windowIncrement = windowIncrement;
  this->deliver_window = m_windowStart;
  this->package_window = m_windowStart;

  this->p_conn = p_conn; // previous conn on this circuit
  this->n_conn = n_conn; // next conn on this circuit

  this->next_active_on_n_conn = 0;
  this->next_active_on_p_conn = 0;

  /*********************************************/
  /*
   * NEW variables for NEW congestion control
   */
  this->m_inflight = 0;
  this->m_pckCounter = 0;
  this->m_num_sendmes = 0;
  
  this->cc_sendme_inc = m_windowIncrement; // 25, 33, 50
  this->cc_cwnd_inc = cc_sendme_inc; // 25,50,100, default: 31, cc_sendme_inc worked best

  this->cc_cwnd_init = m_windowStart; //std::max(cc_sendme_inc, (cc_bwe_min*cc_sendme_inc)); // 150, 200, 250, 500, default: 4*31 TODO
  this->cwnd = cc_cwnd_init; 

  this->cc_cwnd_min = std::max((cc_bwe_min*cc_sendme_inc), cc_sendme_inc); //std::max(31, cc_sendme_inc) ; // [100, 150, 200], default: 31
  this->cc_cwnd_max = INT_MAX; //[5000, 10000, 20000], default: INT_MAX
  this->cc_cwnd_inc_rate = 5; // [1, 2, 5, 10]


  this->cc_bwe_min = 5; // 4-10, default: 5
  cc_bwe_min = std::min(cc_bwe_min, (cc_cwnd_min/cc_sendme_inc));  
  this->cc_ewma_cwnd_pct = 50; // [25,50,100], default: 50, 100
  this->cc_ewma_max = 10; // [10, 20], default: 10

  this->in_slow_start = true;
  this->cc_cwnd_inc_pct_ss = 50; // 50,100,200, default: 50
  this->next_cc_event = 10;

  this->m_num_sendme_timestamp_delta = 0;
  this->m_min_rtt = 100.0; 
  this->m_max_rtt = 0.0; 
  this->m_curr_rtt = 0;

  // WESTWOOD
  this->cc_westwood_rtt_thresh = 33; // [20, 33, 40, 50], default: 33
  this->cc_westwood_min_backoff = false; // [false, true], default: false
  this->cc_westwood_cwnd_m = 0.75; // [50, 66, 75] by 100, deault: 75 (0.75)
  this->cc_westwood_rtt_m = 0.5; // [50, 100] by 100 => DECREASE RTT_max
  // VEGAS
  this->cc_vegas_alpha = 6*cc_sendme_inc - cc_sendme_inc;
  this->cc_vegas_beta =  6*cc_sendme_inc;
  this->cc_vegas_gamma = 6*cc_sendme_inc;
  this->cc_vegas_delta = 6*cc_sendme_inc + 2*cc_sendme_inc;
  // NOLA
  this->cc_nola_overshoot = 100; // 0, 50, 100, 150, 200, default: 100
}


Circuit::~Circuit ()
{
  NS_LOG_FUNCTION (this);
  delete this->p_cellQ;
  delete this->n_cellQ;
}

void
Circuit::DoDispose ()
{
  this->next_active_on_p_conn = 0;
  this->next_active_on_n_conn = 0;
  this->p_conn->SetActiveCircuits (0);
  this->n_conn->SetActiveCircuits (0);
}


Ptr<Packet>
Circuit::PopQueue (queue<Ptr<Packet> > *queue)
{
  if (queue->size () > 0)
    {
      Ptr<Packet> cell = queue->front ();
      queue->pop ();

      return cell;
    }
  return 0;
}


Ptr<Packet>
Circuit::PopCell (CellDirection direction)
{
  Ptr<Packet> cell;
  if (direction == OUTBOUND)
    {
      cell = this->PopQueue (this->n_cellQ);
    }
  else
    {
      cell = this->PopQueue (this->p_cellQ);
    }

  if (cell)
    {
      if (!IsSendme (cell))
        {
          IncrementStats (direction, 0, CELL_PAYLOAD_SIZE);
        }

      /* handle sending sendme cells here (instead of in PushCell) because
       * otherwise short circuits could have more than a window-ful of cells
       * in-flight. Regular circuits will not be affected by this. */
      Ptr<Connection> conn = GetConnection (direction);
      if (!conn->SpeaksCells ())
        {
          deliver_window--;
          if(oldCongControl)
            {
              // OLD CONGESTION CONTROL
              if (deliver_window <= m_windowStart - m_windowIncrement)
                {
                  IncDeliverWindow ();
                  //cout << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << "] Send SENDME cell, Deliver Window now:" << deliver_window << "\n";
                  NS_LOG_LOGIC ("[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << "] Send SENDME cell ");
                  Ptr<Packet> sendme_cell = CreateSendme ();
                  GetQueue (BaseCircuit::GetOppositeDirection (direction))->push (sendme_cell);
                  GetOppositeConnection (direction)->ScheduleWrite ();
                }
            }
          else
            {
              // NEW CONGESTION CONTROL
              if (deliver_window <= cc_cwnd_init - cc_sendme_inc)
                {
                  IncDeliverWindow ();
                  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << 
                            "] Send SENDME cell Increased Deliver window now: " << deliver_window );
                  Ptr<Packet> sendme_cell = CreateSendme ();
                  GetQueue (BaseCircuit::GetOppositeDirection (direction))->push (sendme_cell);
                  GetOppositeConnection (direction)->ScheduleWrite ();
                }
            }
        }
    }

  return cell;
}


void
Circuit::PushCell (Ptr<Packet> cell, CellDirection direction)
{
  if (cell)
    {
      Ptr<Connection> conn = GetConnection (direction);
      Ptr<Connection> opp_conn = GetOppositeConnection (direction);

      if (!opp_conn->SpeaksCells ())
        {
          // OLD CONGESTION CONTROL
          if(oldCongControl)
            {
              package_window--;
              m_pckCounter++; // NEW packet_counter to analyze traffic
              if (package_window <= 0)
                {
                  opp_conn->SetBlocked (true);
                }
                cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << 
                          "] SENT cell. Package window now " << package_window << 
                          " packet_counter: " << m_pckCounter <<"\n";            
            }
          // NEW CONGESTION CONTROL
          else 
            {
              m_pckCounter++;
              m_inflight++;

              // get timestamp for every 50th packet => will be acknowledged with SENDME
              if (m_pckCounter % 50 == 0)
                {
                  sentPck_timestamps[m_pckCounter] = Simulator::Now();
                  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                            "Timestamp for Packet no.: " << m_pckCounter << " is: " << sentPck_timestamps[m_pckCounter] );
                }
              
              if ( (cwnd - m_inflight) <= 0 )
                {
                  opp_conn->SetBlocked (true);
                  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                            "]" << "Opp Con was blocked because cwnd - m_inflight is: " << (cwnd - m_inflight) );
                }
            }
        }  

      if (!conn->SpeaksCells ())
        {
          // delivery
          if (IsSendme (cell))
            {
              NS_LOG_LOGIC ("[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << "] Received SENDME cell." );
              
              // OLD CONGESTION CONTROL
              if(oldCongControl)
                {
                  // update package window
                  IncPackageWindow ();
                  NS_LOG_LOGIC ("[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << "] Received SENDME cell. Package window now " << package_window);
                  //cout << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << "] Received SENDME cell. Package window now " << package_window;
                }

              // NEW CONGESTION CONTROL
              else
                {
                  m_num_sendmes++;
                  m_inflight = m_inflight - cc_sendme_inc;
                  ack_timestamps[m_num_sendmes] = (Simulator::Now()); // add timestamp of this sendme cell

                  // calculate RTT based on new timestamp pair
                  CalculateRtt(conn) ; 
                  
                  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                              "]" << "SENDME no.: " << m_num_sendmes << " Total RTT no.: " << m_raw_rtt.size());
                  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                              "RTT for SENDME no. " << m_num_sendmes << " is: " << m_ewma_rtt.back() << " ACK timestamp: " << ack_timestamps[m_num_sendmes]  );
                  double curr_bdp = cc_cwnd_init;

                  // calculate BDP and update CWND only after min number of packets has been received
                  if (m_num_sendmes >= cc_bwe_min)
                    {
                      curr_bdp = CalculateBDP(conn, direction);
                      NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                                "]" << "Currently used BDP: " << curr_bdp );
                      UpdateCwnd(curr_bdp, conn);
                      cout << " packet_counter: " << m_pckCounter << "\n";
                    }

                  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                            "] Current cwnd: " << cwnd << " pck_counter: " << m_pckCounter << " inflight: " << m_inflight );
                  //cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                  //          "] Current cwnd: " << cwnd << " pck_counter: " << m_pckCounter << "\n";
                }
              
              //cout << " got out of condition\n";
              // re-activate connection (independent of congestion control version)
              if (conn->IsBlocked ())
                {
                  conn->SetBlocked (false);
                  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                             "] Con was activated again because of sendme");
                  conn->ScheduleRead ();
                  //cout << "scheduled read\n";
                }
              // no stats and no cell push on sendme cells
              return;
            }

          CellHeader h;
          cell->RemoveHeader (h);
        }

      IncrementStats (direction, CELL_PAYLOAD_SIZE, 0);
      //cout << "incremented stats\n";
      GetQueue (direction)->push (cell);
      //cout << "got queue\n";
    }
    //cout << "end of pushcell\n";
}

// NEW
/*
 * recursively calculate EWMA smoothing for RTT values over cc_ewma_cwnd_pct rounds
 * Exponentially Weighted Moving Average: statistical measure used to model a time series
 * designed as such that older observations are given lower weights
 * r - indicates the current "round"
 */
double 
Circuit::CalcEWMASmoothingRTT(int r, int n_iter)
{
  double alpha = 0.8;
  double tmp = 0.0;
  if(r < n_iter)
    {
      tmp = alpha * m_raw_rtt.at( m_raw_rtt.size()-r) 
                    + (1-alpha) * CalcEWMASmoothingRTT( r+1, n_iter );
    }
  else
    {
      tmp = alpha * m_raw_rtt.at( m_raw_rtt.size()-r);
    }
  return tmp;
}

// NEW
void 
Circuit::CalculateRtt( Ptr<Connection> conn ) //map<uint16_t,Time> sent_time, map<uint16_t,Time> ack_time, int sendme_cnt)
{
  // get ID of sendme and corresponding packet
  int curr_sendme = m_num_sendmes;
  int pckno_to_sendme = curr_sendme * 50;

  // get timestamps of the sent and acknowledging time of the packet to calculate raw RTT
  Time curr_sent_time = sentPck_timestamps[pckno_to_sendme];
  Time curr_ack_time = ack_timestamps[curr_sendme];
  
  // get raw RTT value 
  Time curr_rtt = curr_ack_time - curr_sent_time; 
  if (curr_rtt <= 0)
    {
      // TODO: add error handling
      NS_ABORT_MSG ("Circuit::CalculateRtt(): Unexpected RTT below zero");
      return;
    }
  
  // save raw current RTT as double value for further processing
  m_curr_rtt = curr_rtt.GetSeconds();
  m_raw_rtt.push_back(m_curr_rtt);
  
  // update min and max RTT (min only after certain number of rounds to allow smoothing)
  if ( m_curr_rtt < m_min_rtt ) // && (int(m_ewma_rtt.size()) >= cc_ewma_cwnd_pct) )//( m_curr_rtt < m_min_rtt && int(m_pckCounter) >= cc_ewma_cwnd_pct)
    {
      m_min_rtt = m_curr_rtt;
      NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                "NEW min RTT: "<< m_min_rtt << " pck no: " << m_pckCounter );
      //cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
      //          "NEW min RTT: "<< m_min_rtt << " pck no: " << m_pckCounter << "\n";
  
    }
  if( m_curr_rtt > m_max_rtt)
    {
      m_max_rtt = m_curr_rtt;
      //cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
      //          "NEW max RTT: "<< m_max_rtt << " pck no: " << m_pckCounter << "\n";
  
    }

  //int no_smoothing = m_raw_rtt.size() * cc_ewma_cwnd_pct / 100;
  int no_smoothing = (cwnd/cc_sendme_inc) * cc_ewma_cwnd_pct / 100;
  no_smoothing = std::max(std::min(no_smoothing, cc_ewma_max), 2);
  // calculate EWMA smoothing over cc_ewma_cwnd_pct values if possible
  if( int(m_raw_rtt.size()) >= no_smoothing )
    { 
      m_curr_rtt = CalcEWMASmoothingRTT(1, no_smoothing);
    }

  // save current RTT (EWMA smoothed if possible)
  m_ewma_rtt.push_back(m_curr_rtt);

  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
              "RTT values:\n curr:" << curr_rtt.GetSeconds() << " EWMA smoothed: " << m_curr_rtt << " min: " << m_min_rtt << " max: " << m_max_rtt);
  return;
}

// NEW
/*
 * recursively calculate EWMA smoothing for BDP values over cc_ewma_cwnd_pct rounds
 * description above
 * r indicates the current "round"
 */
double 
Circuit::CalcEWMASmoothingBDP(int r, int n_iter)
{
  cout << "CALC BDP Smoothing\n";
  double N = cc_ewma_cwnd_pct;
  double tmp = 0.0;
  if(r < n_iter)
    {
      tmp = (2 * m_sendme_bdp_list.at( m_sendme_bdp_list.size()-r) / (N+1) )
                    + ( (N-1) * CalcEWMASmoothingBDP(r+1, n_iter) / (N+1) );
    }
  else
    {
      tmp = (2 * m_sendme_bdp_list.at( m_sendme_bdp_list.size()-r) / (N+1) );
    }
  return tmp;
}

// NEW
double
Circuit::CalculateBDP ( Ptr<Connection> conn, CellDirection direction )
{
  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
              "] flavor: " << m_bdp_flavor);

  // calculate BDP values with different algorithms            
  double bdp = 0;
  double bdp_sendme = 0;
  double bdp_cwnd = 0;
  double bdp_inflight = 0;

  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
              "] BDP values\n sendme: " << bdp_sendme << " cwnd: " << bdp_cwnd << " inflight: " << bdp_inflight);

  // piecewise BDP estimation is used to help respond more quickly in the event
  //    the local OR connection is blocked, which indicates congestion somewhere along
  //    the path from the client to the guard
  if (m_bdp_flavor == "piecewise")
    {
      bdp_sendme = CalculateBDP_sendme ();
      // BDP EWMA smoothing for SENDME estimator
      //int no_smoothing = m_sendme_bdp_list.size() * cc_ewma_cwnd_pct / 100;
      int no_smoothing = (cwnd/cc_sendme_inc) * cc_ewma_cwnd_pct / 100;
      no_smoothing = std::max(std::min(no_smoothing, cc_ewma_max), 2);

      if( int(m_sendme_bdp_list.size()) > no_smoothing)
        {
          bdp_sendme = CalcEWMASmoothingBDP(1, no_smoothing );
        }
      bdp_cwnd = CalculateBDP_cwnd ();
      bdp_inflight = CalculateBDP_inflight (direction);

      // blocked connection => minimum of the inflight and SENDME estimators
      if (conn->IsBlocked())
        {
          bdp = min(bdp_inflight, bdp_sendme);
        }
      // TODO: implement exception handling for sendme estimator
      else
        {
          bdp = max(bdp_sendme, bdp_cwnd);
        }
    }

  else if (m_bdp_flavor == "sendme")
    {
      bdp = CalculateBDP_sendme ();
      // BDP EWMA smoothing for SENDME estimator
      //int no_smoothing = m_sendme_bdp_list.size() * cc_ewma_cwnd_pct / 100;
      int no_smoothing = (cwnd/cc_sendme_inc) * cc_ewma_cwnd_pct / 100;
      no_smoothing = std::max(std::min(no_smoothing, cc_ewma_max), 2);

      if( int(m_sendme_bdp_list.size()) > no_smoothing)
        {
          bdp = CalcEWMASmoothingBDP(1, no_smoothing);
        }
    }

  else if (m_bdp_flavor == "cwnd")
    {
      bdp = CalculateBDP_cwnd ();
    }

  else if (m_bdp_flavor == "inflight")
    {
      bdp = CalculateBDP_inflight ( direction );
    }

  else
    {
      //cout << "TODO: Please choose a valid algorithm to estimate the BDP";
      NS_ABORT_MSG ("Circuit::CalculateBDP(): Invalid BDP algorithm");
    }
  //cout << "BDP values: sendme: " << bdp_sendme << " cwnd: " << bdp_cwnd <<
  //    " inflight: " << bdp_inflight << " piecewise: " << bdp << "\n";
  return bdp;
}

// NEW
double 
Circuit::CalculateBDP_sendme ( )
{
  /*
  TODO:
  If all edge connections no longer have data available to send on a circuit
  and all circuit queues have drained without blocking the local orconn, we stop
  updating this BDP estimate and discard old timestamps. However, we retain the
  actual estimator value.
  */
  double bwe = 0;
  double bdp = 0;
  double sendme_ack_timestamp_delta = 0;
  double t_last = 0;
  double t_prev = 0;

  int ack_map_size = ack_timestamps.size();

  t_last = (ack_timestamps[m_num_sendmes]).GetSeconds();
  
  if ( ack_map_size < 2 )
    {
      bwe = cc_sendme_inc / t_last; // only 1 ack received so far
      //cout << "TIMESTAMPS: t_last " << t_last << " cc_sendme_inc: " << cc_sendme_inc << "\n";
      NS_LOG_LOGIC("TIMESTAMPS: t_last " << t_last << " cc_sendme_inc: " << cc_sendme_inc);
    }
  else
    {
      t_prev = (ack_timestamps[m_num_sendmes-1]).GetSeconds(); 
      sendme_ack_timestamp_delta = t_last - t_prev; 
      
      // averaging BWE Calculation
      m_num_sendme_timestamp_delta = m_num_sendme_timestamp_delta + sendme_ack_timestamp_delta;
      bwe = (m_num_sendmes -1 ) * cc_sendme_inc / m_num_sendme_timestamp_delta;
    }
  bdp = bwe * m_min_rtt;
  return bdp;
}

// NEW
double 
Circuit::CalculateBDP_cwnd ()
{
  double bdp = 0;
  double curr_ewma_rtt = m_ewma_rtt.back();  

  bdp = cwnd * m_min_rtt / curr_ewma_rtt;
  //cout << "BDP CWND calc: " << " cwnd: " << cwnd << " min_rtt " << m_min_rtt <<
  //    " ewma rtt: " << curr_ewma_rtt << "\n";
  /*NS_LOG_LOGIC  (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "BDP cwnd TIMESTAMPS: BDP: " << bdp << " cwnd: " << cwnd << " min RTT: "<< minrtt << " curr rtt: " << curr_rtt);*/
  return bdp; // TODO error fixing because bdp shouldnt be inflight
}

// NEW
double
Circuit::CalculateBDP_inflight( CellDirection direction )
{
  double bdp = 0;
  double curr_ewma_rtt =  m_ewma_rtt.back();
  queue<Ptr<Packet> > *queue = this->GetQueue (direction);
  size_t queue_size = queue->size();

  bdp = (m_inflight - queue_size) * m_min_rtt / curr_ewma_rtt; // TODO substitute 0 by actual number of cells in channel queue
  //cout << "BDP INFLIGHT calc: " << "BDP: " << bdp << " m_inflight: " << m_inflight << 
  //        " queue size: " << queue_size << " min RTT: " << m_min_rtt << " curr rtt: " << curr_ewma_rtt << "\n";
  
  /*NS_LOG_LOGIC  (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "BDP: " << bdp << " m_inflight: " << m_inflight << " min RTT: "<< m_min_rtt << " curr rtt: " << curr_ewma_rtt);*/
  return bdp; // TODO error fixing because bdp shouldnt be inflight
}



Ptr<Connection>
Circuit::GetConnection (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->n_conn;
    }
  else
    {
      return this->p_conn;
    }
}

Ptr<Connection>
Circuit::GetOppositeConnection (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->p_conn;
    }
  else
    {
      return this->n_conn;
    }
}

Ptr<Connection>
Circuit::GetOppositeConnection (Ptr<Connection> conn)
{
  if (this->n_conn == conn)
    {
      return this->p_conn;
    }
  else if (this->p_conn == conn)
    {
      return this->n_conn;
    }
  else
    {
      return 0;
    }
}



CellDirection
Circuit::GetDirection (Ptr<Connection> conn)
{
  if (this->n_conn == conn)
    {
      return OUTBOUND;
    }
  else
    {
      return INBOUND;
    }
}

CellDirection
Circuit::GetOppositeDirection (Ptr<Connection> conn)
{
  if (this->n_conn == conn)
    {
      return INBOUND;
    }
  else
    {
      return OUTBOUND;
    }
}

Ptr<Circuit>
Circuit::GetNextCirc (Ptr<Connection> conn)
{
  // working on conn (above indicatin as this):
  NS_ASSERT (this->n_conn); // assert that circuit has the attirbute n_conn
  // get the circuit whose next conn equals conn
  if (this->n_conn == conn)
    {
      return next_active_on_n_conn;
    }
  else
    {
      // ??? WHY does it have to be the previous circ if it's not the next
      return next_active_on_p_conn;
    }
}


void
Circuit::SetNextCirc (Ptr<Connection> conn, Ptr<Circuit> circ)
{
  if (this->n_conn == conn)
    {
      next_active_on_n_conn = circ;
    }
  else
    {
      next_active_on_p_conn = circ;
    }
}


bool
Circuit::IsSendme (Ptr<Packet> cell)
{
  if (!cell)
    {
      return false;
    }
  CellHeader h;
  cell->PeekHeader (h);
  if (h.GetCmd () == RELAY_SENDME)
    {
      return true;
    }
  return false;
}

Ptr<Packet>
Circuit::CreateSendme ()
{
  CellHeader h;
  h.SetCircId (GetId ());
  h.SetType (RELAY);
  h.SetStreamId (42);
  h.SetCmd (RELAY_SENDME);
  h.SetLength (0);
  Ptr<Packet> cell = Create<Packet> (CELL_PAYLOAD_SIZE);
  cell->AddHeader (h);

  return cell;
}


queue<Ptr<Packet> >*
Circuit::GetQueue (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->n_cellQ;
    }
  else
    {
      return this->p_cellQ;
    }
}


uint32_t
Circuit::GetQueueSize (CellDirection direction)
{
  if (direction == OUTBOUND)
    {
      return this->n_cellQ->size ();
    }
  else
    {
      return this->p_cellQ->size ();
    }
}

uint32_t
Circuit::GetQueueSizeBytes (CellDirection direction)
{
  queue<Ptr<Packet>> * source = (direction == OUTBOUND) ? this->n_cellQ : this->p_cellQ;
  
  // copy temporarily
  queue<Ptr<Packet>> q{*source};

  uint32_t result = 0;
  while (q.size() > 0)
  {
    result += q.front()->GetSize();
    q.pop();
  }
  return result;
}

/*
uint32_t
Circuit::GetPackageWindow ()
{
  if(oldCongControl)
    {
      return package_window;
    }
  // NEW: return delta also here to not change other funtions
  else
    {
      uint32_t delta = cwnd - m_inflight;
      return delta;
    } 
}
*/
uint32_t
Circuit::GetPackageWindow ()
{
  return package_window;
}

uint32_t
Circuit::GetCwnd ()
{
  return cwnd;
}

uint32_t
Circuit::GetInflight ()
{
  return m_inflight;
}

void
Circuit::IncPackageWindow ()
{
  package_window += m_windowIncrement;
  if (package_window > m_windowStart)
    {
      package_window = m_windowStart;
    }
}

uint32_t
Circuit::GetDeliverWindow ()
{
  return deliver_window;
}

void
Circuit::IncDeliverWindow ()
{
  deliver_window += m_windowIncrement;
  if (deliver_window > m_windowStart)
    {
      deliver_window = m_windowStart;
    }
}

// NEW update congestion window
void
Circuit::UpdateCwnd (double curr_bdp, Ptr<Connection> conn)
{
  //cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () << 
  //      "] congestion flavor: " << this->m_cong_flavor << " bdp flavor: " << this->m_bdp_flavor;
  if( this->m_cong_flavor == "nola")
    {
      UpdateCwnd_nola(curr_bdp, conn);
    }
  else if( this->m_cong_flavor == "westwood")
    {
      UpdateCwnd_westwood(curr_bdp, conn);
    }
  else if( this->m_cong_flavor == "vegas")
    {
      UpdateCwnd_vegas(curr_bdp, conn);
    }
  else
    {
      cout << "TODO Please choose a valid algorithm to update the congestion window";
    }
}

// NEW
void
Circuit::UpdateCwnd_westwood (double curr_bdp, Ptr<Connection> conn)
{
  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "] WESTWOOD: Current Cwnd: " << cwnd << " used bdp: " << curr_bdp);
  cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
      "] WESTWOOD: Current Cwnd: " << cwnd << " inflight: " << m_inflight <<
      " dif: " << (cwnd-m_inflight) << " used bdp: " << std::to_string(curr_bdp);
  if (next_cc_event > 0)
    {
      next_cc_event--;
    }
  else if(next_cc_event == 0)
    {
      if ( ( m_curr_rtt < (100-cc_westwood_rtt_thresh)*m_min_rtt/100 
                        + cc_westwood_rtt_thresh*m_max_rtt/100) 
                        || (conn->IsBlocked ()) )
        {
          if (in_slow_start)
            {
              //cout << "first condition + slow start\n";
              //cout << "m_curr_rtt: " << m_curr_rtt << " other term: " << (100-cc_westwood_rtt_thresh)*m_min_rtt/100 + cc_westwood_rtt_thresh*m_max_rtt/100 << 
              //        " blocked: " << conn->IsBlocked() << "\n";
              double cwnd_inc = cwnd * cc_cwnd_inc_pct_ss/100;
              cwnd = cwnd + cwnd_inc;
              //cwnd += cwnd * cc_cwnd_inc_pct_ss; // TODO check += oder = ???
            }
          else
            {
              //cout << "first condition + fast mode\n";
              cwnd = cwnd + cc_cwnd_inc;
            }
        }
      else
        {
          if( cc_westwood_min_backoff )
            {
              //cout << "second condition + backoff_min\n";
              cwnd = std::min( cwnd * cc_westwood_cwnd_m , curr_bdp ); 
            }
          else
            {
              //cout << "second condition + backoff_max\n";
              cwnd = std::max( cwnd * cc_westwood_cwnd_m, curr_bdp );
            }
          in_slow_start = false;
          m_max_rtt = m_min_rtt + cc_westwood_rtt_m * (m_max_rtt - m_min_rtt);
        }
      
      cwnd = std::min(std::max(cwnd, cc_cwnd_min), cc_cwnd_max);
      
      NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                "] WESTWOOD UPDATE cwnd = cwnd + cc_cwnd_inc => max(cwnd * cc_westwood_cwnd_m, curr_bdp)\n" <<
                "UPDATED cwnd = " << cwnd);
      //cout << " UPDATE cwnd: " << cwnd;

      next_cc_event = cwnd / (cc_cwnd_inc_rate * cc_sendme_inc);
    }
}

// NEW
void
Circuit::UpdateCwnd_vegas (double curr_bdp, Ptr<Connection> conn)
{
  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "] VEGAS: Current Cwnd: " << cwnd << " used bdp: " << curr_bdp);
  cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "] VEGAS: Current Cwnd: " << cwnd << " inflight: " << m_inflight << 
            " dif: " << (cwnd-m_inflight) << " used bdp: " << curr_bdp;
  int queue_use = 0;
  if ( next_cc_event > 0)
    {
      next_cc_event--;
    }
  else if(next_cc_event == 0)
    {
      if( curr_bdp > cwnd)
        {
          queue_use = 0;
        }
      else
        {
          queue_use = cwnd - curr_bdp;
        }

      if(in_slow_start)
        {
          if( (queue_use < cc_vegas_gamma) && !(conn->IsBlocked()) )
            {
              //cout << "slow start if: queue use: " << queue_use << " vegas gamma: " << cc_vegas_gamma <<
              //  " blocked: " << conn->IsBlocked() << "\n";
              int cwnd_inc = cwnd * cc_cwnd_inc_pct_ss/100;
              cwnd_inc = std::max(cwnd_inc, 2*cc_sendme_inc);
              cwnd = std::max((cwnd+cwnd_inc), int(curr_bdp)); 
              //cwnd = std::max((cwnd*cc_cwnd_inc_pct_ss), curr_bdp); // TODO: really 100 or rather 1 since it is percentage=> range from 0-1?!
            }
          else
            {
              //cout << "slow start else: queue use: " << queue_use << " vegas gamma: " << cc_vegas_gamma <<
              //  " blocked: " << conn->IsBlocked() << "\n";
              cwnd = curr_bdp + cc_vegas_gamma;
              in_slow_start = false;
            }
        }
      else
        {
          if(queue_use > cc_vegas_delta)
            {
              cwnd = curr_bdp + cc_vegas_delta - cc_cwnd_inc;
            }
          else if( (queue_use > cc_vegas_beta) || (conn->IsBlocked()) )
            {
              //cout << "fast start if: queue use: " << queue_use << " vegas beta: " << cc_vegas_beta <<
              //  " blocked: " << conn->IsBlocked() << "\n";
              //cout << "queue use: " << queue_use << " vegas beta: " << cc_vegas_beta << "\n";
              cwnd -= cc_cwnd_inc;
            }
          else if( queue_use < cc_vegas_alpha )
            {
              //cout << "fast start if: queue use: " << queue_use << " vegas alpha: " << cc_vegas_alpha <<
              //  " blocked: " << conn->IsBlocked() << "\n";
              //cout << "queue use: " << queue_use << " vegas alpha: " << cc_vegas_alpha << "\n";
              cwnd += cc_cwnd_inc;
            }
        }
      cwnd = std::min(std::max(cwnd, cc_cwnd_min), cc_cwnd_max);
      NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                "] VEGAS: UPDATE as CWND = max(cwnd*100, curr_bdp) => cwnd += cc_cwnd_inc (sometimes) => max(cwnd, cc_cwnd_min)\n" << 
                " UPDATED cwnd = " << cwnd );
      /*cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
                "] VEGAS: UPDATE as CWND = max(cwnd*100, curr_bdp) => cwnd += cc_cwnd_inc (sometimes) => max(cwnd, cc_cwnd_min)\n" << 
                " UPDATED cwnd = " << cwnd << "\n";*/

      //cout << " UPDATE cwnd: " << cwnd ;
    
      if(in_slow_start)
        {
          next_cc_event = cwnd/cc_sendme_inc;
        }
      else
        {
          next_cc_event = cwnd / (cc_cwnd_inc_rate * cc_sendme_inc);
        } 
    }
  return;
}

// NEW
void 
Circuit::UpdateCwnd_nola (double curr_bdp, Ptr<Connection> conn)
{
  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "] NOLA: Current Cwnd: " << cwnd << " used bdp: " << curr_bdp);
  cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "] NOLA: Current Cwnd: " << cwnd << " inflight: " << m_inflight <<
            " dif: " << (cwnd-m_inflight) << " used bdp: " << curr_bdp;

  if (conn->IsBlocked ())
    {
      cwnd = curr_bdp;
    }
  else
    {
      cwnd = curr_bdp + cc_nola_overshoot;
    }

  cwnd = std::min(std::max(cwnd, cc_cwnd_min), cc_cwnd_max); 

  NS_LOG_LOGIC (Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "] NOLA: UPDATE as CWND = curr_bdp (+overshoot if not blocked)\n" << 
            " UPDATED cwnd = " << cwnd );
  /*cout << Simulator::Now() << "[" << conn->GetTorApp()->GetNodeName() << ": Circuit " << GetId () <<
            "] NOLA: UPDATE as CWND = curr_bdp (+overshoot if not blocked)\n" << 
            " UPDATED cwnd = " << cwnd << "\n";*/

  //cout << " update cwnd: " << cwnd ;
  return;
}


uint32_t
Circuit::SendCell (CellDirection direction)
{
  queue<Ptr<Packet> >* cellQ = GetQueue (direction);
  if (cellQ->size () <= 0)
    {
      return 0;
    }

  Ptr<Connection> conn = GetConnection (direction);
  if (conn->IsBlocked () || conn->GetSocket ()->GetTxAvailable () < CELL_NETWORK_SIZE)
    {
      return 0;
    }

  Ptr<Packet> cell = PopCell (direction);
  return conn->GetSocket ()->Send (cell);
}








Connection::Connection (TorApp* torapp, Ipv4Address ip, int conntype)
{
  this->torapp = torapp;
  this->remote = ip;
  this->inbuf.size = 0;
  this->outbuf.size = 0;
  this->reading_blocked = 0;
  this->active_circuits = 0;

  m_socket = 0;
  m_conntype = conntype;
}


Connection::~Connection ()
{
  NS_LOG_FUNCTION (this);
}

Ptr<Circuit>
Connection::GetActiveCircuits ()
{
  return active_circuits;
}

void
Connection::SetActiveCircuits (Ptr<Circuit> circ)
{
  active_circuits = circ;
}


uint8_t
Connection::GetType ()
{
  return m_conntype;
}

bool
Connection::SpeaksCells ()
{
  return m_conntype == RELAYEDGE;
}

bool
Connection::IsBlocked ()
{
  return reading_blocked;
}

void
Connection::SetBlocked (bool b)
{
  reading_blocked = b;
}


Ptr<Socket>
Connection::GetSocket ()
{
  return m_socket;
}

void
Connection::SetSocket (Ptr<Socket> socket)
{
  m_socket = socket;
}

Ipv4Address
Connection::GetRemote ()
{
  return remote;
}

map<Ipv4Address,string> Connection::remote_names;

void
Connection::RememberName (Ipv4Address address, string name)
{
  Connection::remote_names[address] = name;
}

string
Connection::GetRemoteName ()
{
  if (Ipv4Mask ("255.0.0.0").IsMatch (GetRemote (), Ipv4Address ("127.0.0.1")) )
  {
    stringstream ss;
    // GetRemote().Print(ss);
    ss << GetActiveCircuits()->GetId();
    if(DynamicCast<PseudoServerSocket>(GetSocket()))
    {
      ss << "-server";
    }
    else
    {
      NS_ASSERT(DynamicCast<PseudoClientSocket>(GetSocket()));
      ss << "-client";
    }
    
    return string("pseudo-") + ss.str();
  }

  map<Ipv4Address,string>::const_iterator it = Connection::remote_names.find(GetRemote ());
  NS_ASSERT(it != Connection::remote_names.end() );
  string name{it->second};
  
  stringstream result;
  result << name;

  // Add the circuit ID to the connection name if it there is only one circuit
  auto first_circuit = GetActiveCircuits();
  if (first_circuit && (first_circuit->GetNextCirc(this) == first_circuit))
  {
    result << "." << first_circuit->GetId();
  }

  return result.str();
}



uint32_t
Connection::Read (vector<Ptr<Packet> >* packet_list, uint32_t max_read)
{
  if (reading_blocked)
    {
      //cout << "[" << torapp->GetNodeName() << ": Connection " << GetRemoteName () << "] Reading nothing: blocked\n";
      NS_LOG_LOGIC ("[" << torapp->GetNodeName() << ": Connection " << GetRemoteName () << "] Reading nothing: blocked");
      return 0;
    }

  uint8_t raw_data[max_read + this->inbuf.size];
  memcpy (raw_data, this->inbuf.data, this->inbuf.size);
  uint32_t tmp_available = m_socket->GetRxAvailable ();

  int read_bytes = m_socket->Recv (&raw_data[this->inbuf.size], max_read, 0);

  uint32_t base = SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint32_t datasize = read_bytes + inbuf.size;
  uint32_t leftover = datasize % base;
  int num_packages = datasize / base;

  NS_LOG_LOGIC ("[" << torapp->GetNodeName() << ": Connection " << GetRemoteName () << "] " <<
      "GetRxAvailable = " << tmp_available << ", "
      "max_read = " << max_read << ", "
      "read_bytes = " << read_bytes << ", "
      "base = " << base << ", "
      "datasize = " << datasize << ", "
      "leftover = " << leftover << ", "
      "num_packages = " << num_packages
  );

  // slice data into packets
  Ptr<Packet> cell;
  for (int i = 0; i < num_packages; i++)
    {
      cell = Create<Packet> (&raw_data[i * base], base);
      packet_list->push_back (cell);
    }

  //safe leftover
  memcpy (inbuf.data, &raw_data[datasize - leftover], leftover);
  inbuf.size = leftover;

  return read_bytes;
}

void
Connection::CountFinalReception(int circid, uint32_t length)
{
  if (!SpeaksCells() && DynamicCast<PseudoClientSocket>(GetSocket()))
  {
    // notify about bytes leaving the network
    torapp->m_triggerBytesLeftNetwork(torapp, circid, data_index_last_delivered[circid] + 1, data_index_last_delivered[circid] + (uint64_t)length);
    data_index_last_delivered[circid] += (uint64_t) length;
  }
}


uint32_t
Connection::Write (uint32_t max_write)
{
  uint32_t base = SpeaksCells () ? CELL_NETWORK_SIZE : CELL_PAYLOAD_SIZE;
  uint8_t raw_data[outbuf.size + (max_write / base + 1) * base];
  memcpy (raw_data, outbuf.data, outbuf.size);
  uint32_t datasize = outbuf.size;
  int written_bytes = 0;

  // fill raw_data
  bool flushed_some = false;
  Ptr<Circuit> start_circ = GetActiveCircuits ();
  NS_ASSERT (start_circ);
  Ptr<Circuit> circ;
  Ptr<Packet> cell = Ptr<Packet> (NULL);
  CellDirection direction;

  NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": Connection " << GetRemoteName () << "] Trying to write cells from circuit queues");

  while (datasize < max_write)
    {
      circ = GetActiveCircuits ();
      NS_ASSERT (circ);

      direction = circ->GetDirection (this);
      cell = circ->PopCell (direction);
      int circid = circ->GetId();

      if (cell)
        {
          uint32_t cell_size = cell->CopyData (&raw_data[datasize], cell->GetSize ());

          // check if just packaged??? WHY, WHERE?
          auto opp_con = circ->GetOppositeConnection(direction);
          if (!opp_con->SpeaksCells() && DynamicCast<PseudoServerSocket>(opp_con->GetSocket()))
          {
            // notify about bytes entering the network
            torapp->m_triggerBytesEnteredNetwork(torapp, circid, data_index_last_seen[circid] + 1, data_index_last_seen[circid] + cell_size);
          }

          datasize += cell_size;

          flushed_some = true;

          data_index_last_seen[circid] += cell_size;
          NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": Connection " << GetRemoteName () << "] Actually sending one cell from circuit " << circ->GetId ());
        }

      SetActiveCircuits (circ->GetNextCirc (this));

      if (GetActiveCircuits () == start_circ)
        {
          if (!flushed_some)
            {
              break;
            }
          flushed_some = false;
        }
    }

  // send data
  max_write = min (max_write, datasize);
  if (max_write > 0)
    {
      written_bytes = m_socket->Send (raw_data, max_write, 0);
    }
  NS_ASSERT(written_bytes >= 0);

  /* save leftover for next time */
  written_bytes = max (written_bytes,0);
  uint32_t leftover = datasize - written_bytes;
  memcpy (outbuf.data, &raw_data[datasize - leftover], leftover);
  outbuf.size = leftover;

  if(leftover > 0)
    NS_LOG_LOGIC ("[" << torapp->GetNodeName () << ": Connection " << GetRemoteName () << "] " << leftover << " bytes left over for next conn write");

  return written_bytes;
}


void
Connection::ScheduleWrite (Time delay)
{
  if (m_socket && write_event.IsExpired ())
    {
      write_event = Simulator::Schedule (delay, &TorApp::ConnWriteCallback, torapp, m_socket, m_socket->GetTxAvailable ());
    }
}

void
Connection::ScheduleRead (Time delay)
{
  if (m_socket && read_event.IsExpired ())
    {
      read_event = Simulator::Schedule (delay, &TorApp::ConnReadCallback, torapp, m_socket);
    }
}


uint32_t
Connection::GetOutbufSize ()
{
  return outbuf.size;
}

uint32_t
Connection::GetInbufSize ()
{
  return inbuf.size;
}

} //namespace ns3
