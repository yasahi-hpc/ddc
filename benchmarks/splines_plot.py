# Copyright (C) The DDC development team, see COPYRIGHT.md file
#
# SPDX-License-Identifier: MIT

# First execute :
# ./benchmarks/ddc_benchmark_splines --benchmark_format=json --benchmark_out=splines_bench.json
# then execute this code will be able to plot results:
# python3 splines_plot.py /path/to/splines_bench.json

import argparse
from operator import itemgetter 
import matplotlib.pyplot as plt
import json
import numpy as np

parser = argparse.ArgumentParser(description="Plot bytes_per_second from a JSON file.")
parser.add_argument("json_file", help="Path to the JSON file")
args = parser.parse_args()

with open(args.json_file, 'r') as file:
        data = json.load(file);

nx_values = sorted(set(int(benchmark["name"].split("/")[4]) for benchmark in data["benchmarks"]))
data_dict = [{
"on_gpu": int(benchmark["name"].split("/")[1]),
"nx": int(benchmark["name"].split("/")[4]),
"ny": int(benchmark["name"].split("/")[5]),
"cols_per_chunk": int(benchmark["name"].split("/")[6]),
"preconditionner_max_block_size": int(benchmark["name"].split("/")[7]),
"bytes_per_second": benchmark["bytes_per_second"],
"gpu_mem_occupancy": benchmark["gpu_mem_occupancy"]
} for benchmark in data["benchmarks"]]

plotter = lambda plt, x_name, y_name, data_dict_sorted, filter : plt.plot([item[x_name] for item in data_dict_sorted if filter(item)], [item[y_name] for item in data_dict_sorted if filter(item)], marker='o', markersize=5, label=f'nx={nx}')

########
## ny ##
########

data_dict_sorted = sorted(data_dict, key=itemgetter("nx","ny"))
plt.figure(figsize=(16, 6))

plt.subplot(1, 2, 1)
for nx in nx_values:
	plotter(plt, "ny", "bytes_per_second", data_dict_sorted, lambda item : item["nx"]==nx and not item["on_gpu"])

ny_min = min([item["ny"] for item in data_dict_sorted if item["on_gpu"]])
if len([item for item in data_dict_sorted if item["ny"]==ny_min and not item["on_gpu"]]) != 0:
    x = np.linspace(ny_min, 20*ny_min)
    plt.plot(x, np.mean([item["bytes_per_second"] for item in data_dict_sorted if item["ny"]==ny_min and not item["on_gpu"]])/ny_min*x, linestyle='--', color='black', label='perfect scaling')

plt.grid()
plt.xscale("log")
plt.xlabel("ny")
plt.ylabel("Throughput [B/s]")
plt.title("Throughput on CPU")
plt.legend()

plt.subplot(1, 2, 2)
for nx in nx_values:
	plotter(plt, "ny", "bytes_per_second", data_dict_sorted, lambda item : item["nx"]==nx and item["on_gpu"])

ny_min = min([item["ny"] for item in data_dict_sorted if item["on_gpu"]])
if len([item for item in data_dict_sorted if item["ny"]==ny_min and item["on_gpu"]]) != 0:
    x = np.linspace(ny_min, 20*ny_min)
    plt.plot(x, np.mean([item["bytes_per_second"] for item in data_dict_sorted if item["ny"]==ny_min and item["on_gpu"]])/ny_min*x, linestyle='--', color='black', label='perfect scaling')

plt.grid()
plt.xscale("log")
plt.xlabel("ny")
plt.ylabel("Throughput [B/s]")
plt.title("Throughput on GPU")
plt.legend()
plt.savefig("throughput_ny.png")

#############
## gpu_mem ##
#############

plt.figure(figsize=(8, 6))

for nx in nx_values:
	filter = lambda item : item["nx"]==nx and item["on_gpu"] and item["ny"]>=8e3
	plt.plot([item["ny"] for item in data_dict_sorted if filter(item)], [(item["gpu_mem_occupancy"]-nx*item["ny"]*8)/(nx*item["ny"]*8)*100 for item in data_dict_sorted if filter(item)], marker='o', markersize=5, label=f'nx={nx}')

plt.grid()
plt.xscale("log")
plt.xlabel("ny")
plt.ylabel("Relative memory overhead [%]")
plt.title("Relative memory occupancy overhead")
plt.legend()
plt.savefig("gpu_mem_occupancy.png")

########################
## cols_per_chunk ##
########################

data_dict_sorted = sorted(data_dict, key=itemgetter("nx","cols_per_chunk"))
plt.figure(figsize=(16, 6))

plt.subplot(1, 2, 1)
for nx in nx_values:
	plotter(plt, "cols_per_chunk", "bytes_per_second", data_dict_sorted, lambda item : item["nx"]==nx and not item["on_gpu"])

# Plotting the data
plt.grid()
plt.xscale("log")
plt.xlabel("cols_per_chunk")
plt.ylabel("Throughput [B/s]")
plt.title("Throughput on CPU (with ny="+str([item["ny"] for item in data_dict_sorted][0])+")");
plt.legend()

plt.subplot(1, 2, 2)
for nx in nx_values:
	plotter(plt, "cols_per_chunk", "bytes_per_second", data_dict_sorted, lambda item : item["nx"]==nx and item["on_gpu"])

# Plotting the data
plt.grid()
plt.xscale("log")
plt.xlabel("cols_per_chunk")
plt.ylabel("Throughput [B/s]")
plt.title("Throughput on GPU (with ny="+str([item["ny"] for item in data_dict_sorted][0])+")");
plt.legend()
plt.savefig("throughput_cols.png")

#####################
## preconditionner ##
#####################

data_dict_sorted = sorted(data_dict, key=itemgetter("nx","cols_per_chunk"))
plt.figure(figsize=(16, 6))

plt.subplot(1, 2, 1)
for nx in nx_values:
	plotter(plt, "preconditionner_max_block_size", "bytes_per_second", data_dict_sorted, lambda item : item["nx"]==nx and not item["on_gpu"])

# Plotting the data
plt.grid()
plt.xscale("log")
plt.xlabel("preconditionner_max_block_size")
plt.ylabel("Throughput [B/s]")
plt.title("Throughput on CPU (with ny="+str([item["ny"] for item in data_dict_sorted][0])+")");
plt.legend()

plt.subplot(1, 2, 2)
for nx in nx_values:
	plotter(plt, "preconditionner_max_block_size", "bytes_per_second", data_dict_sorted, lambda item : item["nx"]==nx and item["on_gpu"])

# Plotting the data
plt.grid()
plt.xscale("log")
plt.xlabel("cols_per_chunk")
plt.ylabel("Throughput [B/s]")
plt.title("Throughput on GPU (with ny="+str([item["ny"] for item in data_dict_sorted][0])+")");
plt.legend()
plt.savefig("throughput_cols.png")

plt.close();
