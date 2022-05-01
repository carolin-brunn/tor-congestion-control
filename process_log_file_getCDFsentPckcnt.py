#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Wed Mar 23 21:52:38 2022

@author: carolin
"""

import pandas as pd
import numpy as np
import re
import matplotlib.pyplot as plt
import seaborn as sb

numeric_const_pattern = r"[-+]?\d*\.?\d+|[-+]?\d+"
rx = re.compile(numeric_const_pattern, re.VERBOSE)

def process_row(l, idx):
    tmp = float((rx.findall(l[0]))[idx])
    #l_no.append(numbers)
    return tmp

def load_data_new(path, merge_dict, n_circ, run, cong, datarate_dict, relay):
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
        merge_dict["cong_flavor"].extend([cong] * len(l_exit_time))
        
        final_cnt = l_exit_pckcnt[len(l_exit_pckcnt)-1]
        first_time = l_exit_time[0]
        last_time = l_exit_time[len(l_exit_time)-1]
        delta = (last_time - first_time) *1.0E-9
        data_rate = final_cnt / delta
        
        datarate_dict["circuit"].append(str(c))
        datarate_dict["run"].append(str(run))
        datarate_dict["cong_flavor"].append(cong)
        datarate_dict["final_count"].append(final_cnt)
        datarate_dict["data_rate"].append(data_rate)

            
        
def load_data_old(path, merge_dict, n_circ, run, datarate_dict):
    df = pd.read_csv(path, header = None)
    
    for c in range(1,n_circ+1):
        df_circ = df[df[0].str.contains(("Circuit " + str(c)))] 
        df_circ = df_circ[df_circ[0].str.contains("exit")] 
        df_circ = df_circ.reset_index(drop = True)
        
        ### extract numerical values ###
        l_exit_time = df_circ.apply(lambda row: process_row(row, 0), axis=1)
        l_exit_pckwdw = df_circ.apply(lambda row: process_row(row, 3), axis=1)
        l_exit_pckcnt = df_circ.apply(lambda row: process_row(row, 4), axis=1)
        
        merge_dict["time"].extend(l_exit_time)
        merge_dict["packet_wdw"].extend(l_exit_pckwdw)
        merge_dict["packet_cnt"].extend(l_exit_pckcnt)
        merge_dict["circuit"].extend(([str(c)]*len(l_exit_time)))
        merge_dict["run"].extend([str(run)]*len(l_exit_time))
        
        final_cnt = l_exit_pckcnt[len(l_exit_pckcnt)-1]
        first_time = l_exit_time[0]
        last_time = l_exit_time[len(l_exit_time)-1]
        delta = (last_time - first_time) *1.0E-9
        data_rate = final_cnt / delta
        cong = "old"
        
        datarate_dict["circuit"].append(str(c))
        datarate_dict["run"].append(str(run))
        datarate_dict["cong_flavor"].append(cong)
        datarate_dict["final_count"].append(final_cnt)
        datarate_dict["data_rate"].append(data_rate)

    
    
def main():

    sim_t = "60s"
    l_congFlavor = ["westwood", "nola", "vegas", "westwoodmin"]
    bdpFlavor = "piecewise" #//sendme/cwnd/inflight
    bwRate = "6"
    bwBurst = "8"
    n_circ = 5
    oldCongControl = "0"
    n_runs = 10
    rtt = 100
    relay = "exit"
    
    path = "/home/carolin/Schreibtisch/uni/masterProject/git/my-predictor-congestion/"
    
    ''' NEW CONGESTION CONTROL'''
    merge_dict_new = {"time":[],
                  "cwnd": [],
                  "inflight": [],
                  "bdp": [],
                  "packet_cnt": [],
                  "circuit": [],
                  "run": [], 
                  "cong_flavor": []}
    
    datarate_dict = {"circuit": [],
                     "run": [], 
                     "cong_flavor": [],
                     "final_count":[],
                     "data_rate":[]}
    
    for congFlavor in l_congFlavor:
        
        for run in range(1, n_runs+1):
            print("load data tor: ", congFlavor, " run: ", run)
            fn = "tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + "_" + "rate" + bwRate + "_burst" + bwBurst + "_ncirc" + str(n_circ) + "_oldCong" + oldCongControl + "_run" + str(run) + "_rtt" + str(rtt) + ".txt"
            load_data_new((path+fn), merge_dict_new, n_circ, run, congFlavor, datarate_dict, relay)
    

    merged_df_new = pd.DataFrame(merge_dict_new)
    print(merged_df_new.head())
    
    #dict_name = "tor_simu_pckCnt_BDP" + bdpFlavor + "_nCirc" + str(n_circ) + "_simt" + sim_t + ".csv"
    #merged_df_new.to_csv(dict_name, sep = ',')
    
    '''OLD CONTROL'''
    merge_dict_old = {"time":[],
                  "packet_wdw": [],
                  "packet_cnt": [],
                  "circuit":[],
                  "run":[]}
    
    for run in range(1, n_runs+1):
        print("load data")
        fn = "tor_simu_" + sim_t + "_rate" + bwRate + "_burst" + bwBurst + "_ncirc" + str(n_circ) + "_oldCong1" + "_run" + str(run) + "_rtt" + str(rtt) + ".txt"
        load_data_old((path+fn), merge_dict_old, n_circ, run, datarate_dict)
        
    merged_df_old = pd.DataFrame(merge_dict_old)
    print(merged_df_old.head())
    
    dict_name = "tor_simu_pckCnt_BDP" + bdpFlavor + "_nCirc" + str(n_circ) + "_simt" + sim_t + "_rtt" + str(rtt) + ".csv"
    merged_df_old.to_csv(dict_name, sep = ',')
    
    datarate_df = pd.DataFrame(datarate_dict)
    dict_name = "tor_simu_finalCnt_BDP" + bdpFlavor + "_nCirc" + str(n_circ) + "_simt" + sim_t + "_rtt" + str(rtt) + ".csv"
    datarate_df.to_csv(dict_name, sep = ',')
    
    ''' CREATE CDF'''
    
    # getting data of the histogram
    tmp = merged_df_new[(merged_df_new["cong_flavor"] == "vegas")]
    count_veg, bins_count_veg = np.histogram(tmp["packet_cnt"], bins=100)
    # finding the PDF of the histogram using count values
    pdf_veg = count_veg / sum(count_veg)
    # using numpy np.cumsum to calculate the CDF
    # We can also find using the PDF values by looping and adding
    cdf_veg = np.cumsum(pdf_veg)
    
    tmp = merged_df_new[(merged_df_new["cong_flavor"] == "nola")]
    count_no, bins_count_no = np.histogram(tmp["packet_cnt"], bins=100)
    pdf_no = count_no / sum(count_no)
    cdf_no = np.cumsum(pdf_no)
    
    tmp = merged_df_new[(merged_df_new["cong_flavor"] == "westwood")]
    count_we, bins_count_we = np.histogram(tmp["packet_cnt"], bins=100)
    pdf_we = count_we / sum(count_we)
    cdf_we = np.cumsum(pdf_we)
    
    
    tmp = merged_df_new[(merged_df_new["cong_flavor"] == "westwoodmin")]
    count_wemin, bins_count_wemin = np.histogram(tmp["packet_cnt"], bins=100)
    pdf_wemin = count_wemin / sum(count_wemin)
    cdf_wemin = np.cumsum(pdf_wemin)
    
    tmp = merged_df_old
    count_old, bins_count_old = np.histogram(tmp["packet_cnt"], bins=100)
    pdf_old = count_old / sum(count_old)
    cdf_old = np.cumsum(pdf_old)
    
    
    plt.title("CDF Packet count, BDP flavor: " + bdpFlavor + ", Circuits: "+ str(n_circ))
    plt.plot(bins_count_no[1:], cdf_no, color="blue", linestyle = "-", label="nola")
    plt.plot(bins_count_veg[1:], cdf_veg, color="orange", linestyle = "--", label="vegas")
    plt.plot(bins_count_we[1:], cdf_we, color="green", linestyle=":", label="westwood")
    plt.plot(bins_count_wemin[1:], cdf_wemin, color="purple", linestyle = (0, (3, 2, 1, 2, 3)), label="westwood (min)")
    plt.plot(bins_count_old[1:], cdf_old, color="orangered", linestyle = "-.", label="original")
    plt.legend(loc=4)
    
    plt.savefig("plt_CDF_sent_pckcnt_bdp" + bdpFlavor + "_nCirc" + str(n_circ) + "_simt" + sim_t + "_rate" + bwRate + "_burst" + bwBurst+ "_run" + str(run) + "_rtt" + str(rtt) +".pdf")
    plt.close()
    
    """
    FINAL COUNT
    """
    
    tmp = datarate_df
    count_old, bins_count_old = np.histogram(tmp["final_count"], bins=100)
    pdf_old = count_old / sum(count_old)
    cdf_old = np.cumsum(pdf_old)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "vegas")]
    count_veg, bins_count_veg = np.histogram(tmp["final_count"], bins=100)
    # finding the PDF of the histogram using count values
    pdf_veg = count_veg / sum(count_veg)
    # using numpy np.cumsum to calculate the CDF
    # We can also find using the PDF values by looping and adding
    cdf_veg = np.cumsum(pdf_veg)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "nola")]
    count_no, bins_count_no = np.histogram(tmp["final_count"], bins=100)
    pdf_no = count_no / sum(count_no)
    cdf_no = np.cumsum(pdf_no)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "westwood")]
    count_we, bins_count_we = np.histogram(tmp["final_count"], bins=100)
    pdf_we = count_we / sum(count_we)
    cdf_we = np.cumsum(pdf_we)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "westwoodmin")]
    count_wemin, bins_count_wemin = np.histogram(tmp["final_count"], bins=100)
    pdf_wemin = count_wemin / sum(count_wemin)
    cdf_wemin = np.cumsum(pdf_wemin)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "old")]
    count_old, bins_count_old = np.histogram(tmp["final_count"], bins=100)
    pdf_old = count_old / sum(count_old)
    cdf_old = np.cumsum(pdf_old)
    
    
    plt.title("CDF Final count, BDP flavor: " + bdpFlavor + ", Circuits: "+ str(n_circ))
    plt.plot(bins_count_no[1:], cdf_no, color="blue", linestyle = "-", label="nola")
    plt.plot(bins_count_veg[1:], cdf_veg, color="orange", linestyle = "--", label="vegas")
    plt.plot(bins_count_we[1:], cdf_we, color="green", linestyle=":", label="westwood")
    plt.plot(bins_count_wemin[1:], cdf_wemin, color="purple", linestyle = (0, (3, 2, 1, 2, 3)), label="westwood (min)")
    plt.plot(bins_count_old[1:], cdf_old, color="orangered", linestyle = "-.", label="original")
    plt.xlabel("final count [pck]")
    plt.legend(loc=4)
    
    plt.savefig("plt_CDF_sent_finalcnt_bdp" + bdpFlavor + "_nCirc" + str(n_circ) + "_simt" + sim_t + "_rate" + bwRate + "_burst" + bwBurst+ "_run" + str(run) + "_rtt" + str(rtt)+".pdf")
    plt.close()
    
    """
    DATA RATE
    """
    
    tmp = datarate_df
    count_old, bins_count_old = np.histogram(tmp["data_rate"], bins=100)
    pdf_old = count_old / sum(count_old)
    cdf_old = np.cumsum(pdf_old)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "vegas")]
    count_veg, bins_count_veg = np.histogram(tmp["data_rate"], bins=100)
    # finding the PDF of the histogram using count values
    pdf_veg = count_veg / sum(count_veg)
    # using numpy np.cumsum to calculate the CDF
    # We can also find using the PDF values by looping and adding
    cdf_veg = np.cumsum(pdf_veg)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "nola")]
    count_no, bins_count_no = np.histogram(tmp["data_rate"], bins=100)
    pdf_no = count_no / sum(count_no)
    cdf_no = np.cumsum(pdf_no)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "westwood")]
    count_we, bins_count_we = np.histogram(tmp["data_rate"], bins=100)
    pdf_we = count_we / sum(count_we)
    cdf_we = np.cumsum(pdf_we)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "westwoodmin")]
    count_wemin, bins_count_wemin = np.histogram(tmp["data_rate"], bins=100)
    pdf_wemin = count_wemin / sum(count_wemin)
    cdf_wemin = np.cumsum(pdf_wemin)
    
    tmp = datarate_df[(datarate_df["cong_flavor"] == "old")]
    count_old, bins_count_old = np.histogram(tmp["data_rate"], bins=100)
    pdf_old = count_old / sum(count_old)
    cdf_old = np.cumsum(pdf_old)
    
    plt.title("CDF Data Rate, BDP flavor: " + bdpFlavor + ", Circuits: "+ str(n_circ))
    plt.plot(bins_count_no[1:], cdf_no, color="blue", linestyle = "-", label="nola")
    plt.plot(bins_count_veg[1:], cdf_veg, color="orange", linestyle = "--", label="vegas")
    plt.plot(bins_count_we[1:], cdf_we, color="green", linestyle=":", label="westwood")
    plt.plot(bins_count_wemin[1:], cdf_wemin, color="purple", linestyle = (0, (3, 2, 1, 2, 3)), label="westwood (min)")
    plt.plot(bins_count_old[1:], cdf_old, color="orangered", linestyle = "-.", label="original")
    plt.xlabel("data rate [pck/s]")
    plt.legend(loc=4)
    
    plt.savefig("plt_CDF_sent_datarate_bdp" + bdpFlavor + "_nCirc" + str(n_circ) + "_simt" + sim_t + "_rate" + bwRate + "_burst" + bwBurst+ "_run" + str(run) + "_rtt" + str(rtt)+".pdf")
    plt.close()

    
    
if __name__ == "__main__":
    main()