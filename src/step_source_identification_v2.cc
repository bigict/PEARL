#include "step_source_identification_v2.h"

#include <algorithm>
#include <vector>
#include <string>
#include <memory>
#include "unistd.h"

#include "runner.h"
#include "runner_manager.h"
#include "parameter.h"
#include "undirected_graph.h"
#include "network.h"
#include "pearl_network.h"
#include "static_network.h"
#include "dynamic_network.h"
#include "simulator.h"
#include "source_identification.h"
#include "util.h"

StepSourceIdentificationV2 StepSourceIdentificationV2::_step_source_identification_v2;

StepSourceIdentificationV2::StepSourceIdentificationV2(): Runner() {
	_short_options = "h";
	_long_options = new struct option[100]{
		{"help",		no_argument,		NULL, OPT_HELP},
//		{"net_type",	required_argument,	NULL, OPT_NET_TYPE},
		{"net_inroot",	required_argument,	NULL, OPT_NET_INROOT},
//		{"net_injson",	required_argument,	NULL, OPT_NET_INJSON},
//		{"net_volunteers",required_argument,NULL, OPT_NET_VOLUNTEERS},
		{"out_dir",		required_argument,	NULL, OPT_OUT_DIR},
		{"disease",		required_argument,	NULL, OPT_DISEASE},
		{"infect_rate",	required_argument,	NULL, OPT_INFECT_RATE},
		{"infect_rate_seconds",	required_argument,	NULL, OPT_INFECT_RATE_SECONDS},
//		{"infectious_rate",	required_argument,	NULL, OPT_INFECTIOUS_RATE},
//		{"infectious_rate_seconds",	required_argument,	NULL, OPT_INFECTIOUS_RATE_SECONDS},
		{"recover_rate",	required_argument,	NULL, OPT_RECOVER_RATE},
		{"recover_rate_seconds",	required_argument,	NULL, OPT_RECOVER_RATE_SECONDS},
		{"seconds_per_weight",	required_argument, NULL, OPT_SECONDS_PER_WEIGHT},
		{"seconds_per_step",	required_argument, NULL, OPT_SECONDS_PER_STEP},
//		{"source_count",	required_argument, NULL, OPT_SOURCE_COUNT},
//		{"snapshot_coverage",	required_argument, NULL, OPT_SNAPSHOT_COVERAGE},
		{"max_sim_days",	required_argument, NULL, OPT_MAX_SIM_DAYS},
		{"repeat_times",	required_argument, NULL, OPT_REPEAT_TIMES},
//		{"source_identification_method", required_argument, NULL, OPT_SRC_IDN_METHOD},
//		{"source_identification_knowntime", required_argument, NULL, OPT_SRC_IDN_KNOWNTIME},
		{NULL,			0,					NULL,  0 } //must end with {0, 0, 0, 0}
	};
	RunnerManager::instance()->install("step_source_identification_v2", this);
}

void StepSourceIdentificationV2::help() {
	std::cout << "\nFunctionality: source identification. In this version, different source identification methods are compared in the exactly same simulations, thus this version is better.\n";
	std::cout << "Option list:\n";
	std::cout << "\t* --help (or -h): [ no argument ] print this help information.\n";
	std::cout << "\t* --net_inroot: [ string argument ] root directory or file of the network(s).\n";
	std::cout << "\t* --out_dir: [ string argument ] output directory of the results.\n";
	std::cout << "\t* --disease: [ string argument ] disease model of the problem, possible values are si, sis, sir, seir.\n";
	std::cout << "\t* --infect_rate: [ double argument ] infect rate, should be in the range (0, 1).\n";
	std::cout << "\t* --infect_rate_seconds: [ int/double argument ] duration of the parameter infect_rate, should be a positive value.\n";
	std::cout << "\t* --recover_rate: [ double argument ] recover rate, should be in the range (0, 1).\n";
	std::cout << "\t* --recover_rate_seconds: [ int/double argument ] duration of the parameter recover_rate, should be a positive value.\n";
	std::cout << "\t* --seconds_per_weight: [ int/double argument ] duration denoted by unit weight on the edge, should be a positive value.\n";
	std::cout << "\t* --seconds_per_step: [ int/double argument ] duration denoted by one step in the simulating process, should be a positive value.\n";
	std::cout << "\t* --max_sim_days: [ int argument ] maximal simulating duration, should be a positive value. Simulation stops once simulating time reaches this value.\n";
	std::cout << "\t* --repeat_times: [ int argument ] repeat times of simulation for every parameter setting, should be a positive value.\n";
	//std::cout << "\t* --source_identification_method: [ string argument ] method of the source identification, possible values are sse, ssebfs, tse, msep, msepbfs, urcc, sjc, jce, mjc, rg, da, ub, aub, dmp, bp, mcsm, sleuth.\n";
	//std::cout << "\t* --source_identification_knowntime: [ string argument ] denotes whether spreading time is supplied as input in the source identification problem, possible values are true, false.\n";
	std::cout << std::endl;
}


int StepSourceIdentificationV2::run(const Parameter& para) {
	// Read network
	std::shared_ptr<Network> net = std::make_shared<StaticNetwork>(para);
	bool is_tree = net->get_merged_graph()->is_tree();
	std::vector<std::string> idn_methods;
	if (is_tree) {
		idn_methods = {"sse", "ssebfs", "sjc", "jce", "rg", "da", "ub", "dmp", "mcsm", "sleuth"};
	} else {
		idn_methods = {"sse", "ssebfs", "rg", "da", "ub", "dmp", "mcsm", "sleuth"};
	}
	//idn_methods = {"dmp", "mcsm"};
	// Repeat simulation and source identification
	std::vector<SourceIdentificationResV2> res_structs(idn_methods.size());
	int repeat_times = para.get_repeat_times();
	for (int i = 0; i < idn_methods.size(); ++i) {
		res_structs[i].method_name = idn_methods[i];
		res_structs[i].running_times.resize(repeat_times);
		res_structs[i].detection_rates.resize(repeat_times);
		res_structs[i].error_distances.resize(repeat_times);
	}

	int n= net->get_node_size();
	clock_t start_clock, end_clock;
	for (int i = 0; i < repeat_times; ++i) {
		std::cout << "Round " << i + 1 << "/" << repeat_times << std::endl;
		std::string true_seed_node;
		std::vector<DiseaseStage> sim_res;
		int infected_count;
		// get a simulation result which has at least 10 infected nodes.
		do {
			// Init seed
			int true_seed_index = Util::gen_rand_int(n);
			true_seed_node = net->get_node_name(true_seed_index);
			IndexSet seed_set{true_seed_index};
			// Init simulator
			Simulator simulator(para, n, seed_set);
			// Simulate the disease spreading
			sim_res = simulator.get_sim_res(*net, para);
			infected_count = Simulator::get_nodeset_been_infected_from_sim_res(sim_res, para.get_disease(), *net).size();
		} while (infected_count < 10);
		// try every possible method
		for (int j = 0; j < idn_methods.size(); ++j) {
			// Infer seed and record time usage
			start_clock = clock();
			std::string inferred_seed_node = SourceIdentification::calc_source(*net, sim_res, para, UtilConstant::toSrcIdnMethod(idn_methods[j]), false);
			end_clock = clock();
			// Calculate error distance, detection rate, and running time
			double error_distance = SourceIdentification::calc_error_distance(*net, true_seed_node, inferred_seed_node);
			double detection_rate = SourceIdentification::calc_detection_rate(true_seed_node, inferred_seed_node);
			double running_time = 1000.0 * (end_clock - start_clock) / CLOCKS_PER_SEC;
			std::cout << "Method " << idn_methods[j] << ", Time " << running_time / 1000.0 << " s." << std::endl;
			res_structs[j].running_times[i] = running_time;
			res_structs[j].detection_rates[i] = detection_rate;
			res_structs[j].error_distances[i] = error_distance;
		}
	}
	// Calculate error distance, detection rate, running time.
	
	for (int j = 0; j < idn_methods.size(); ++j) {
		res_structs[j].running_time_mean = Util::getMean(res_structs[j].running_times);
		res_structs[j].running_time_sigma = Util::getDeviation(res_structs[j].running_times, res_structs[j].running_time_mean);
		res_structs[j].detection_rate = Util::getMean(res_structs[j].detection_rates);
		res_structs[j].error_distance_mean = Util::getMean(res_structs[j].error_distances);
		res_structs[j].error_distance_sigma = Util::getDeviation(res_structs[j].error_distances, res_structs[j].error_distance_mean);
	}
	// Write results to files.
	write_result(para.get_out_dir(), res_structs);
	return 0;
}

void StepSourceIdentificationV2::write_result(const std::string& res_dir, const std::vector<SourceIdentificationResV2>& res_structs) {
	std::string details_dir = res_dir + "/details/", summary = res_dir + "/summary.txt";
	std::string mkdir_command = "mkdir -p " + details_dir;
	int sys_val = system(mkdir_command.c_str());
	// write details
	for (int j = 0; j < res_structs.size(); ++j) {
		std::string details = details_dir + res_structs[j].method_name + ".txt";
		std::ofstream ofs(details.c_str());
		if (ofs.fail()) {
			std::cerr << "Error writing file: " << details << std::endl;
			exit(-1);
		}
		ofs << "#index detection_rate error_distance running_time(ms)" << std::endl;
		for (int i = 0; i < res_structs[j].error_distances.size(); ++i) {
			ofs << i + 1 << " " << res_structs[j].detection_rates[i] << " " << res_structs[j].error_distances[i] << " " << res_structs[j].running_times[i] << std::endl;
		}
		ofs.close();
	}

	// write summary
	std::ofstream ofs(summary.c_str());
	if (ofs.fail()) {
		std::cerr << "Error writing file: " << summary << std::endl;
		exit(-1);
	}
	ofs << "#Method detection_rate error_distance_mean error_distance_sigma running_time_mean running_time_sigma" << std::endl;
	for (int i = 0; i < res_structs.size(); ++i) {
		ofs << res_structs[i].method_name << " ";
		ofs << res_structs[i].detection_rate << " ";
		ofs << res_structs[i].error_distance_mean << " ";
		ofs << res_structs[i].error_distance_sigma << " ";
		ofs << res_structs[i].running_time_mean << " ";
		ofs << res_structs[i].running_time_sigma << std::endl;
	}
	ofs.close();
}
