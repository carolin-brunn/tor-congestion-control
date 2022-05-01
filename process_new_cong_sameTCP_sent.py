#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Thu Mar 31 13:04:45 2022

@author: carolin
"""

# -*- coding: utf-8 -*-
"""
Spyder Editor

This is a temporary script file.
"""

import pandas as pd
import numpy as np
import re
import matplotlib.pyplot as plt
import seaborn as sb


numeric_const_pattern = r"[-+]?\d*\.?\d+|[-+]?\d+"
rx = re.compile(numeric_const_pattern, re.VERBOSE)

#r"""[-+]? # optional sign 
#       (?:
#        (?: \d* \. \d+ ) # .1 .12 .123 etc 9.1 etc 98.1 etc |
#        (?: \d+ \.? ) # 1. 12. 123. etc 1 12 123 etc
#        # followed by optional exponent part if desired
#        (?: [Ee] [+-]? \d+ ) ?
#        )"""

#EOQ(D,p,ck,ch):
def process_row(l, idx):
    tmp = float((rx.findall(l[0]))[idx])
    return tmp
    
def load_data(path, merge_dict, n_circ, run, relay):
    df = pd.read_csv(path, header = None)
    
    for c in range(1,n_circ+1):
        df_circ = df[df[0].str.contains(("Circuit " + str(c)))] 
        df_circ = df_circ[df_circ[0].str.contains(relay)] 
        df_circ = df_circ[df_circ[0].str.contains("SENT")] 
        df_circ = df_circ.reset_index(drop = True)
        
        ### extract numerical values ###
        l_exit_time = df_circ.apply(lambda row: process_row(row, 0), axis=1)
        l_exit_cwnd = df_circ.apply(lambda row: process_row(row, 3), axis=1)
        l_exit_inflight = df_circ.apply(lambda row: process_row(row, 4), axis=1)
        l_exit_bdp = df_circ.apply(lambda row: process_row(row, 6), axis=1)
        l_exit_pckcnt = df_circ.apply(lambda row: process_row(row, 7), axis=1)
        
        merge_dict["time"].extend(l_exit_time)
        merge_dict["cwnd"].extend(l_exit_cwnd)
        merge_dict["inflight"].extend(l_exit_inflight)
        merge_dict["bdp"].extend(l_exit_bdp)
        merge_dict["packet_cnt"].extend(l_exit_pckcnt)
        merge_dict["circuit"].extend(([str(c)]*len(l_exit_time)))
        merge_dict["run"].extend([str(run)]*len(l_exit_time))

    
    
def main():

    sim_t = "60s"
    congFlavor = "vegas" #westwood/nola/vegas
    bdpFlavor = "piecewise" #//sendme/cwnd/inflight
    bwRate = "6"
    bwBurst = "8"
    n_circ = 1
    oldCongControl = "0"
    n_runs = 10
    rtt = 100
    relay = "exit"
    
    path = "/home/carolin/Schreibtisch/uni/masterProject/git/my-predictor-congestion/"
    
    merge_dict = {"time":[],
                  "cwnd": [],
                  "inflight": [],
                  "bdp": [],
                  "packet_cnt": [],
                  "circuit": [],
                  "run": []}
    
    for run in range(1, n_runs+1):
        print("load data")
        fn = "tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + "_" + "rate" + bwRate + "_burst" + bwBurst + "_ncirc" + str(n_circ) + "_oldCong" + oldCongControl + "_run" + str(run) + "_rtt" + str(rtt) + ".txt"
        load_data((path+fn), merge_dict, n_circ, run, relay)
    

    merged_df = pd.DataFrame(merge_dict)
    print(merged_df.head())
    

    # general settings for plots
    sb.set_style("whitegrid")
    sb.set_palette("colorblind")
    
    ### CONGESTION WINDOW ###
    plt.title("Congestion Window, Tor: " + congFlavor + ", BDP: "+ bdpFlavor  + ", Circuits: "+ str(n_circ))
   
    g = sb.lineplot(data = merged_df, x = "time", y = "cwnd", hue = "circuit", style = "run")
    h,l = g.get_legend_handles_labels()
    plt.legend(h[0:2],l[0:2], loc=4, borderaxespad=0.) #bbox_to_anchor=(1.05, 1),
    g.set(xlabel="time [ns]", ylabel="congestion window [pck]")
    #g.set_axis_labels("time [ns]", "congestion window [pck]")
    #plt.xlabel("time [ns]")
    #plt.ylabel("congestion window [pck]")
    
    plt_name = "plt_CWND_tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + "_" + "rate" + bwRate + "_burst" + bwBurst + "_ncirc" + str(n_circ) + "_oldCong" + oldCongControl + "_run" + str(run) + "_rtt" + str(rtt)+ ".pdf"
    fig = g.figure
    fig.savefig(plt_name)#, bbox_inches = "tight", pad_inches=0.5)
    plt.close(fig)
    
    ### INFLIGHT ###
    plt.title("Inflight, Tor: " + congFlavor + ", BDP: "+ bdpFlavor  + ", Circuits: "+ str(n_circ))
    
    g = sb.lineplot(data = merged_df, x = "time", y = "inflight", hue = "circuit", style = "run")
    h,l = g.get_legend_handles_labels()
    plt.legend(h[0:2],l[0:2], loc=4, borderaxespad=0.) #bbox_to_anchor=(1.05, 1),
    g.set(xlabel="time [ns]", ylabel="inflight [pck]")
    #g.xlabel("time [ns]")
    #g.ylabel("inflight [pck]")
    
    plt_name = "plt_INFLIGHT_tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + "_" + "rate" + bwRate + "_burst" + bwBurst + "_ncirc" + str(n_circ) + "_oldCong" + oldCongControl + "_run" + str(run) + "_rtt" + str(rtt)+ ".pdf"
    fig = g.figure
    fig.savefig(plt_name)#, bbox_inches = "tight", pad_inches=0.5)
    plt.close(fig)
    
    ### PACKET COUNT ###
    plt.title("Packet count, Tor: " + congFlavor + ", BDP: "+ bdpFlavor  + ", Circuits: "+ str(n_circ))
   
    g = sb.lineplot(data = merged_df, x = "time", y = "packet_cnt", hue = "circuit", style = "run")
    h,l = g.get_legend_handles_labels()
    plt.legend(h[0:2],l[0:2], loc=4, borderaxespad=0.) #bbox_to_anchor=(1.05, 1),
    g.set(xlabel="time [ns]", ylabel="packet count")
    #g.xlabel("time [ns]")
    #g.ylabel("packet count")
    
    plt_name = "plt_PCKCNT_tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + "_" + "rate" + bwRate + "_burst" + bwBurst + "_ncirc" + str(n_circ) + "_oldCong" + oldCongControl + "_run" + str(run) + "_rtt" + str(rtt)+ "_lowthresh.pdf"
    fig = g.figure
    fig.savefig(plt_name)#, bbox_inches = "tight", pad_inches=0.5)
    plt.close(fig)
    

    
if __name__ == "__main__":
    main()