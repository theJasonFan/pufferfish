#include "ProgOpts.hpp"
#include <iostream>

int pufferfishStats(pufferfish::StatsOptions& opts) {
	auto index_dir = opts.index_dir;
	auto stats_out = opts.stats_out;

	std::cout << "index_dir: " << index_dir << '\n';
	std::cout << "stats_out: " << stats_out << '\n';
    std::cout << "I'm a smol fish\n";

    return 0;
}