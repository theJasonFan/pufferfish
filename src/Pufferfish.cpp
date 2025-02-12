//
// Pufferfish - An efficient compacted dBG index
//
// Copyright (C) 2017 Rob Patro, Fatemeh Almodaresi, Hirak Sarkar
//
// This file is part of Pufferfish.
//
// RapMap is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// RapMap is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with RapMap.  If not, see <http://www.gnu.org/licenses/>.
//

#include "clipp.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <clocale>
#include <ghc/filesystem.hpp>
//#include <cereal/archives/json.hpp>

#include "PufferfishConfig.hpp"
#include "ProgOpts.hpp"
#include "Util.hpp"
//#include "IndexHeader.hpp"

int pufferfishIndex(pufferfish::IndexOptions& indexOpts); // int argc, char* argv[]);
int pufferfishTest(pufferfish::TestOptions& testOpts);    // int argc, char* argv[]);
int pufferfishValidate(
                       pufferfish::ValidateOptions& validateOpts); // int argc, char* argv[]);
int pufferfishTestLookup(
                         pufferfish::ValidateOptions& lookupOpts); // int argc, char* argv[]);
int pufferfishAligner(pufferfish::AlignmentOpts& alignmentOpts) ;
int pufferfishExamine(pufferfish::ExamineOptions& examineOpts);

int main(int argc, char* argv[]) {
  using namespace clipp;
  using std::cout;
  std::setlocale(LC_ALL, "en_US.UTF-8");

  enum class mode {help, index, validate, lookup, align, examine};
  mode selected = mode::help;
  pufferfish::AlignmentOpts alignmentOpt ;
  pufferfish::IndexOptions indexOpt;
  //TestOptions testOpt;
  pufferfish::ValidateOptions validateOpt;
  pufferfish::ValidateOptions lookupOpt;
  pufferfish::ExamineOptions examineOpt;

  auto ensure_file_exists = [](const std::string& s) -> bool {
      bool exists = ghc::filesystem::exists(s);
      if (!exists) {
        std::string e = "The required input file " + s + " does not seem to exist.";
        throw std::runtime_error{e};
      }
      return true;
  };

  auto ensure_index_exists = [](const std::string& s) -> bool {
      bool exists = ghc::filesystem::exists(s);
      if (!exists) {
        std::string e = "The index directory " + s + " does not seem to exist.";
        throw std::runtime_error{e};
      }
      bool isDir = ghc::filesystem::is_directory(s);
      if (!isDir) {
          std::string e = s + " is not a directory containing index files.";
          throw std::runtime_error{e};
      }
      for (auto & elem : {pufferfish::util::MPH,
                          pufferfish::util::SEQ,
                          pufferfish::util::RANK,
                          pufferfish::util::POS,
                          pufferfish::util::CTABLE,
                          pufferfish::util::REFSEQ,
                          pufferfish::util::REFNAME,
                          pufferfish::util::REFLENGTH,
                          pufferfish::util::REFACCUMLENGTH}) {
          if (!ghc::filesystem::exists(s+"/"+elem)) {
              std::string e = "Index is incomplete. Missing file ";
              e+=elem;
              throw std::runtime_error{e};
          }
      }
      return true;
  };



  auto indexMode = (
                    command("index").set(selected, mode::index),
                    (required("-r", "--ref") & values(ensure_file_exists, "ref_file", indexOpt.rfile)) % "path to the reference fasta file",
                    (required("-o", "--output") & value("output_dir", indexOpt.outdir)) % "directory where index is written",
                    //(required("-g", "--gfa") & value("gfa_file", indexOpt.gfa_file)) % "path to the GFA file",
                    (option("--headerSep") & value("sep_strs", indexOpt.header_sep)) %
                    "Instead of a space or tab, break the header at the first "
                    "occurrence of this string, and name the transcript as the token before "
                    "the first separator (default = space & tab)",
                    (option("--keepDuplicates").set(indexOpt.keep_duplicates) % "Retain duplicate references in the input"),
                    (option("-d", "--decoys") & value("decoy_list", indexOpt.decoy_file)) %
                    "Treat these sequences as decoys that may be sequence-similar to some known indexed reference",
                    (option("-f", "--filt-size") & value("filt_size", indexOpt.filt_size)) % "filter size to pass to TwoPaCo when building the reference dBG",
                    (option("--tmpdir") & value("twopaco_tmp_dir", indexOpt.twopaco_tmp_dir)) % "temporary work directory to pass to TwoPaCo when building the reference dBG",
                    (option("-k", "--klen") & value("kmer_length", indexOpt.k))  % "length of the k-mer with which the dBG was built (default = 31)",
                    (option("-p", "--threads") & value("threads", indexOpt.p))  % "total number of threads to use for building MPHF (default = 16)",
                    (option("-l", "--build-edges").set(indexOpt.buildEdgeVec, true) % "build and record explicit edge table for the contaigs of the ccdBG (default = false)"),
                    (((option("-s", "--sparse").set(indexOpt.isSparse, true)) % "use the sparse pufferfish index (less space, but slower lookup)",
                     ((option("-e", "--extension") & value("extension_size", indexOpt.extensionSize)) % "length of the extension to store in the sparse index (default = 4)")) |
                     ((option("-x", "--lossy-rate").set(indexOpt.lossySampling, true)) & value("lossy_rate", indexOpt.lossy_rate) % "use the lossy sampling index with a sampling rate of x (less space and fast, but lower sensitivity)"))
                    );

  // Examine properties of the index
  auto examineMode = (
                      command("examine").set(selected, mode::examine),
                      (required("-i", "--index") & value("index", examineOpt.index_dir)) % "pufferfish index directory",
                      (option("--dump-fasta") & value("fasta_out", examineOpt.fasta_out)) %
                      "dump the reference sequences in the index in the provided fasta file",
                      (option("--dump-kmer-freq") & value("kmer_freq_out", examineOpt.kmer_freq_out)) %
                      "dump the frequency histogram of k-mers"
                      );
  /*
  auto testMode = (
                   command("test").set(selected, mode::test)
                   );
  */
  auto validateMode = (
                       command("validate").set(selected, mode::validate),
                       (required("-i", "--index") & value("index", validateOpt.indexDir)) % "directory where the pufferfish index is stored");/*,
                       (required("-r", "--ref") & value("ref", validateOpt.refFile)) % "fasta file with reference sequences",
                       (required("-g", "--gfa") & value("gfa", validateOpt.gfaFileName)) % "GFA file name needed for edge table validation"
                       );
                                                                                                                                              */
  auto lookupMode = (
                     command("lookup").set(selected, mode::lookup),
                     (required("-i", "--index") & value("index", lookupOpt.indexDir)) % "directory where the pufferfish index is stored",
                     (required("-r", "--ref") & value("ref", lookupOpt.refFile)) % "fasta file with reference sequences"
                     );

  std::string throwaway;
  auto isValidRatio = [](const char* s) -> void {
    float r{0.0};
    std::string sv(s);
    try {
      r = std::stof(sv);
    } catch (std::exception& e) {
      std::string m = "Could not convert " + sv + " to a valid ratio\n";
      throw std::domain_error(m);
    }
    if (r <= 0 or r > 1) {
      std::string m = "The --scoreRatio you provided was " + sv + ", it must be in (0,1]\n";
      throw std::domain_error(m);
    }
  };

  auto alignMode = (
                    command("align").set(selected, mode::align),
                    (required("-i", "--index") & value(ensure_index_exists, "index", alignmentOpt.indexDir)) % "Directory where the Pufferfish index is stored",
                    (
                      (
                        ((required("--mate1", "-1") & value("mate 1", alignmentOpt.read1)) % "Path to the left end of the read files"),
                        ((required("--mate2", "-2") & value("mate 2", alignmentOpt.read2)) % "Path to the right end of the read files")
                      ) 
                      |
                      ((required("--read").set(alignmentOpt.singleEnd, true) & value("reads", alignmentOpt.unmatedReads)) % "Path to single-end read files")
                    ),
                    (option("-b", "--batchOfReads").set(alignmentOpt.listOfReads, true)) % "Is each input a file containing the list of reads? (default=false)",


                    (option("--coverageScoreRatio") & value("score ratio", alignmentOpt.scoreRatio).call(isValidRatio)) % "Discard mappings with a coverage score < scoreRatio * OPT (default=0.6)",
                    (option("-t", "--threads") & value("num threads", alignmentOpt.numThreads)) % "Specify the number of threads (default=8)",
                    (option("-m", "--just-mapping").set(alignmentOpt.justMap, true)) % "don't attempt alignment validation; just do mapping",
                    (
                      (required("--noOutput").set(alignmentOpt.noOutput, true)) % "Run without writing SAM file"
                      |
                      (required("-o", "--outdir") & value("output file", alignmentOpt.outname)) % "Output file where the alignment results will be stored"
                    ),
                    (option("--maxSpliceGap") & value("max splice gap", alignmentOpt.maxSpliceGap)) % "Specify maximum splice gap that two uni-MEMs should have",
                    (option("--maxFragmentLength") & value("max frag length", alignmentOpt.maxFragmentLength)) % 
                            "Specify the maximum distance between the last uni-MEM of the left and first uni-MEM of the right end of the read pairs (default:1000)",
                    (option("--noOrphans").set(alignmentOpt.noOrphan, true)) % "Write Orphans flag",
                    (option("--orphanRecovery").set(alignmentOpt.recoverOrphans, true)) % "Recover mappings for the other end of orphans using alignment",
                    (option("--noDiscordant").set(alignmentOpt.noDiscordant, true)) % "Write Orphans flag",
		            (option("-z", "--compressedOutput").set(alignmentOpt.compressedOutput, true)) % "Compress (gzip) the output file",
                    (
                      (option("-k", "--krakOut").set(alignmentOpt.krakOut, true)) % "Write output in the format required for krakMap"
                      |
                      (option("-p", "--pam").set(alignmentOpt.salmonOut, true)) % "Write output in the format required for salmon"
                    ),
					(option("--verbose").set(alignmentOpt.verbose, true)) % "Print out auxilary information to trace program's flow",
                    (option("--fullAlignment").set(alignmentOpt.fullAlignment, true)) % "Perform full alignment instead of gapped alignment",
                    (option("--heuristicChaining").set(alignmentOpt.heuristicChaining, true)) % "Whether or not perform only 2 rounds of chaining",
                    (option("--bestStrata").set(alignmentOpt.bestStrata, true)) % "Keep only the alignments with the best score for each read",
					(option("--genomicReads").set(alignmentOpt.genomicReads, true)) % "Align genomic dna-seq reads instead of RNA-seq reads",
					(option("--primaryAlignment").set(alignmentOpt.primaryAlignment, true).set(alignmentOpt.bestStrata, true)) % "Report at most one alignment per read",
                    (option("--filterGenomics").set(alignmentOpt.filterGenomics, true) & value("genes names file", alignmentOpt.genesNamesFile)) % 
                         "Path to the file containing gene IDs. Filters alignments to the IDs listed in the file. Used to filter genomic reads while aligning to both genome and transcriptome."
                         "A read will be reported with only the valid gene ID alignments and will be discarded if the best alignment is to an invalid ID"
                         "The IDs are the same as the IDs in the fasta file provided for the index construction phase",
                    (option("--filterBestScoreMicrobiome").set(alignmentOpt.filterMicrobiomBestScore, true) & value("genes ID file", alignmentOpt.genesNamesFile)) % "Path to the file containing gene IDs. Same as option \"filterGenomics\" except that a read will be discarded if aligned equally best to a valid and invalid gene ID.",
                    (option("--filterMicrobiome").set(alignmentOpt.filterMicrobiom, true) & value("genes ID file", alignmentOpt.rrnaFile)) % "Path to the file containing gene IDs. Same as option \"filterGenomics\" except that a read will be discarded if an invalid gene ID is in the list of alignments.",
                    (option("--bt2DefaultThreshold").set(alignmentOpt.mimicBt2Default, true)) % "mimic the default threshold function of Bowtie2 which is t = -0.6 -0.6 * read_len",
                    (option("--minScoreFraction") & value("minScoreFraction", alignmentOpt.minScoreFraction)) % "Discard alignments with alignment score < minScoreFraction * max_alignment_score for that read (default=0.65)",
                    (option("--consensusFraction") & value("consensus fraction", alignmentOpt.consensusFraction)) % "The fraction of mems, relative to the reference with "
                    "the maximum number of mems, that a reference must contain in order "
                    "to move forward with computing an optimal chain score (default=0.65)"
  );

  auto cli = (
              (indexMode | validateMode | lookupMode | alignMode | examineMode | command("help").set(selected,mode::help) ),
              option("-v", "--version").call([]{std::cout << "version " << pufferfish::version << "\n"; std::exit(0);}).doc("show version"));

  decltype(parse(argc, argv, cli)) res;
  try {
    res = parse(argc, argv, cli);
  } catch (std::exception& e) {
    std::cout << "\n\nParsing command line failed with exception: " << e.what() << "\n";
    std::cout << "\n\n";
    std::cout << make_man_page(cli, pufferfish::progname);
    return 1;
  }


  if(res) {
    switch(selected) {
    case mode::index: pufferfishIndex(indexOpt);  break;
    case mode::validate: pufferfishValidate(validateOpt);  break;
    case mode::lookup: pufferfishTestLookup(lookupOpt); break;
    case mode::align: pufferfishAligner(alignmentOpt); break;
    case mode::examine: pufferfishExamine(examineOpt); break;
    case mode::help: std::cout << make_man_page(cli, pufferfish::progname); break;
    }
  } else {
    auto b = res.begin();
    auto e = res.end();
    if (std::distance(b,e) > 0) {
      if (b->arg() == "index") {
        std::cout << make_man_page(indexMode, pufferfish::progname);
      } else if (b->arg() == "validate") {
        std::cout << make_man_page(validateMode, pufferfish::progname);
      } else if (b->arg() == "lookup") {
        std::cout << make_man_page(lookupMode, pufferfish::progname);
      } else if (b->arg() == "align") {
        std::cout << make_man_page(alignMode, pufferfish::progname);
      } else {
        std::cout << "There is no command \"" << b->arg() << "\"\n";
        std::cout << usage_lines(cli, pufferfish::progname) << '\n';
        return 1;
      }
    } else {
      std::cout << usage_lines(cli, pufferfish::progname) << '\n';
      return 1;
    }
  }

  return 0;
}
