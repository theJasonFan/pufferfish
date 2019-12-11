#include "ProgOpts.hpp"
#include <iostream>
#include "compact_vector/compact_vector.hpp"

int pufferfishStats(pufferfish::StatsOptions& opts) {
	auto index_dir = opts.index_dir;
	bool out = !opts.stats_out.empty();
	compact::vector<uint64_t> auxInfo_{16};
	std::ostream* fp = out ? new std::ofstream(opts.stats_out) : &std::cout ;

    auxInfo_.deserialize(index_dir + "/extension.bin", false);

	*fp << "index_dir: " << index_dir << '\n';
    *fp << "I'm a smol fish\n";
    *fp << auxInfo_[10] << '\n';

    if (out) delete fp;

    return 0;
}