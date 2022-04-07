#include <iostream>
#include <fstream>

#include "ns3/tor-module.h"

using namespace ns3;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TorExample");

void StatsCallback(TorStarHelper*, Time);

int main (int argc, char *argv[]) {
    uint32_t run = 1;
    std::string sim_t = "120s";
    Time simTime = Time(sim_t);
    uint32_t rtt = 100; // TODO: adjustable parameter for further analysis
    string flavor = "vanilla";
    string congFlavor = "vegas"; //nola/westwood/vegas
    string bdpFlavor = "piecewise"; //sendme/cwnd/inflight
    int oldCongControl = 0; 
    int nCirc = 1;

    LogComponentEnable ("TorExample", LOG_LEVEL_INFO);

    CommandLine cmd;
    cmd.AddValue("run", "run number", run);
    cmd.AddValue("rtt", "hop-by-hop rtt in msec", rtt);
    cmd.AddValue("time", "simulation time", simTime);
    cmd.AddValue("flavor", "Tor flavor", flavor);
    cmd.AddValue("congFlavor", "Congestion flavor", congFlavor);
    cmd.AddValue("bdpFlavor", "BDP flavor", bdpFlavor);
    cmd.AddValue("oldCongControl", "Indicate whether old congestion control shall be used (0/1)", 
                    oldCongControl);
    cmd.AddValue("nCirc", "Number of circuits", nCirc);                
    cmd.Parse(argc, argv);

    SeedManager::SetSeed (12);
    SeedManager::SetRun (run);

    /* set global defaults */
    // GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

    /* defaults for ns3's native Tcp implementation */
    // Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1458));
    // Config::SetDefault ("ns3::TcpSocket::TcpNoDelay", BooleanValue (true));
    // Config::SetDefault ("ns3::DropTailQueue::MaxPackets", UintegerValue (100));

    /* TorApp defaults. Note, this also affects onion proxies. */
    // Config::SetDefault ("ns3::TorBaseApp::BandwidthRate", DataRateValue (DataRate ("12Mbps")));
    // Config::SetDefault ("ns3::TorBaseApp::BandwidthBurst", DataRateValue (DataRate ("12Mbps")));
    Config::SetDefault ("ns3::TorApp::WindowStart", IntegerValue (500));
    Config::SetDefault ("ns3::TorApp::WindowIncrement", IntegerValue (50));
    Config::SetDefault ("Circuit::CongestionFlavor", StringValue(congFlavor));
    Config::SetDefault ("Circuit::BDPFlavor", StringValue(bdpFlavor));
    Config::SetDefault ("Circuit::oldCongControl", IntegerValue(oldCongControl));

    cout << "Set up topology\n";
    NS_LOG_INFO("setup topology");

    TorStarHelper th;
    if (flavor == "pctcp")
        th.SetTorAppType("ns3::TorPctcpApp");
    else if (flavor == "bktap")
        th.SetTorAppType("ns3::TorBktapApp");
    else if (flavor == "n23")
        th.SetTorAppType("ns3::TorN23App");
    else if (flavor == "fair")
        th.SetTorAppType("ns3::TorFairApp");

    // th.DisableProxies(true); // make circuits shorter (entry = proxy), thus the simulation faster
    //th.EnableNscStack(true,"cubic"); // enable linux protocol stack and set tcp flavor
    th.SetRtt(MilliSeconds(rtt)); // set rtt
    // th.EnablePcap(true); // enable pcap logging
    // th.ParseFile ("circuits.dat"); // parse scenario from file

    Ptr<ConstantRandomVariable> m_bulkRequest = CreateObject<ConstantRandomVariable>();
    m_bulkRequest->SetAttribute("Constant", DoubleValue(pow(2,30))); // Wahrscheinlichkeitsverteilung wann Anfragen gestellt werden (hier constant)
    Ptr<ConstantRandomVariable> m_bulkThink = CreateObject<ConstantRandomVariable>();
    m_bulkThink->SetAttribute("Constant", DoubleValue(0));

    double test = m_bulkRequest->GetConstant();
    cout << "bulk Request: " << test << "\n";

    Ptr<UniformRandomVariable> m_startTime = CreateObject<UniformRandomVariable> ();

    m_startTime->SetAttribute ("Min", DoubleValue (0.1));
    m_startTime->SetAttribute ("Max", DoubleValue (1.0));
    // th.SetStartTimeStream (m_startTime); // default start time when no PseudoClientSocket specified

    /* state scenario/ add circuits inline */
    // creates circuits in tor-star-helper.cc
    // AddCircuit (int id, string entryName, string middleName, string exitName, Ptr<PseudoClientSocket> clientSocket)
    // RequestPage is scheduled if ClientSocket is initialized

    /*for(int n = 1; n <= nCirc; n++)
        {
            th.AddCircuit(n,("entry"+std::to_string(n)),"btlnk",("exit"+std::to_string(n)), 
                    CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
        } */
    int n_circ_new = 1;
    th.AddCircuit(n_circ_new,"entry1","btlnk","exit1", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
    n_circ_new++;
    if(n_circ_new <= nCirc)
        {
            th.AddCircuit(n_circ_new,"entry2","btlnk","exit2", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
            n_circ_new++;
        }
    if(n_circ_new <= nCirc)
        {
            th.AddCircuit(n_circ_new,"entry2","btlnk","exit2", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
            n_circ_new++;
        }
    if(n_circ_new <= nCirc)
        {
            th.AddCircuit(n_circ_new,"entry4","btlnk","exit2", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
            n_circ_new++;
        }
    if(n_circ_new <= nCirc)
        {
            th.AddCircuit(n_circ_new,"entry5","btlnk","exit5", CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTime->GetValue ())) );
            n_circ_new++;
        }


    string bwRate = "6";
    string bwBurst = "8";
    th.SetRelayAttribute("btlnk", "BandwidthRate", DataRateValue(DataRate((bwRate + "Mb/s"))));
    th.SetRelayAttribute("btlnk", "BandwidthBurst", DataRateValue(DataRate((bwBurst + "Mb/s"))));

    // th.PrintCircuits();
    th.BuildTopology(); // finally build topology, setup relays and seed circuits
    cout << "topology built\n";


    // change standard output to save output in csv file
    std::string filename = " ";
    if(oldCongControl)
    {
        filename = "tor_simu_" + sim_t + "_rate" + bwRate + "_burst" + bwBurst + 
                            "_ncirc" + std::to_string(nCirc) + 
                            "_oldCong" + std::to_string(oldCongControl) + 
                            "_run" + std::to_string(run) + "_rtt" + std::to_string(rtt) + ".txt";
    } 
    else
    {
        filename = "tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + 
                            "_rate" + bwRate + "_burst" + bwBurst + "_ncirc" + std::to_string(nCirc) + 
                            "_oldCong" + std::to_string(oldCongControl) + 
                            "_run" + std::to_string(run) + "_rtt" + std::to_string(rtt) + "_lowthresh.txt";
    }    
    std::cout << (filename) << "\n";
    std::ofstream out(filename);
    std::streambuf *coutbuf = std::cout.rdbuf(); //save old buf
    std::cout.rdbuf(out.rdbuf()); //redirect std::cout to filename!
    
    ApplicationContainer relays = th.GetTorAppsContainer();
    relays.Start (Seconds (0.0)); // set start time of relay Apps = TorApps ( Arrange for all of the Applications in this container to Start() at the Time given as a parameter. )

    relays.Stop (simTime);
    Simulator::Stop (simTime);

    //Simulator::Schedule(Seconds(0), &StatsCallback, &th, simTime);
	//cout << "start simulation\n";
    NS_LOG_INFO("start simulation");
    Simulator::Run (); // scheduled Apps in ApplicationContainer are called 

    NS_LOG_INFO("stop simulation");
    Simulator::Destroy ();

    std::cout.rdbuf(coutbuf); //reset to standard output again

    return 0;
}

/* example of (cumulative) i/o stats */
void
StatsCallback(TorStarHelper* th, Time simTime)
{
    cout << "StatsCallBack: " << Simulator::Now().GetSeconds() << " ";
    vector<int>::iterator id;
    for (id = th->circuitIds.begin(); id != th->circuitIds.end(); ++id) {
      Ptr<TorBaseApp> proxyApp = th->GetProxyApp(*id);
      Ptr<TorBaseApp> exitApp = th->GetExitApp(*id);
      Ptr<BaseCircuit> proxyCirc = proxyApp->baseCircuits[*id];
      Ptr<BaseCircuit> exitCirc = exitApp->baseCircuits[*id];
      cout << "exitCirc BRead Inbound: " << exitCirc->GetBytesRead(INBOUND) << " proxyCirc BWritten Inbound" << proxyCirc->GetBytesWritten(INBOUND) << " ";
      // cout << proxyCirc->GetBytesRead(OUTBOUND) << " " << exitCirc->GetBytesWritten(OUTBOUND) << " ";
      // proxyCirc->ResetStats(); exitCirc->ResetStats();
    }
    cout << endl;

    Time resolution = MilliSeconds(10);
    if (Simulator::Now()+resolution < simTime)
        Simulator::Schedule(resolution, &StatsCallback, th, simTime);
}
