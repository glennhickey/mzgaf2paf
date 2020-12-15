#include <unistd.h>
#include <getopt.h>
#include <fstream>
#include <iostream>
#include <unordered_set>

#include "rgfa-split.hpp"

using namespace std;

void help(char** argv) {
  cerr << "usage: " << argv[0] << " [options]" << endl
       << "Partition rGFA nodes into reference contigs.  Input must be uncompressed GFA (not stdin)" << endl
       << "input options: " << endl
       << "    -g, --rgfa FILE                     rGFA to use as baseline for contig splitting" << endl
       << "    -m, --input-contig-map FILE         Use tsv map (computed with -M) instead of rGFA" << endl
       << "    -p, --paf FILE                      PAF file to split" << endl
       << "output options: " << endl 
       << "    -r, --output-prefix PREFIX          All output files will be of the form <PREFIX><contig>.paf/contigs" << endl
       << "    -M, --output-contig-map FILE        Output rgfa node -> contig map to this file" << endl
       << "contig selection options: " << endl
       << "    -q, --contig-prefix PREFIX          Only process contigs beginning with PREFIX" << endl
       << "    -c, --contig-name NAME              Only process NAME (multiple allowed)" << endl
       << "    -C, --contig-file FILE              Path to list of contigs to process" << endl
       << endl;
}    

int main(int argc, char** argv) {

    // input
    string rgfa_path;
    string input_contig_map_path;
    string input_paf_path;

    // output
    string output_prefix;
    string output_contig_map_path;

    // contig selection
    string contig_prefix;
    unordered_set<string> contig_names;
    string contig_names_path;
    size_t selection_options = 0;
    
    int c;
    optind = 1; 
    while (true) {

        static const struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"rgfa", required_argument, 0, 'g'},
            {"input-contig-map", required_argument, 0, 'm'},
            {"paf", required_argument, 0, 'p'},
            {"output-contig-map", required_argument, 0, 'M'},
            {"contig-prefix", required_argument, 0, 'q'},
            {"contig-name", required_argument, 0, 'c'},
            {"contig-file", required_argument, 0, 'C'},
            {0, 0, 0, 0}
        };

        int option_index = 0;

        c = getopt_long (argc, argv, "hg:m:p:M:q:c:C:",
                         long_options, &option_index);

        // Detect the end of the options.
        if (c == -1)
            break;

        switch (c)
        {
        case 'g':
            rgfa_path = optarg;
            break;
        case 'm':
            input_contig_map_path = optarg;
            break;
        case 'p':
            input_paf_path = optarg;
            break;
        case 'M':
            output_contig_map_path = optarg;
            break;
        case 'q':
            contig_prefix = optarg;
            break;
        case 'c':
            contig_names.insert(optarg);
            break;
        case 'C':
            contig_names_path = optarg;
            break;
        case 'h':
        case '?':
            /* getopt_long already printed an error message. */
            help(argv);
            exit(1);
            break;
        default:
            abort ();
        }
    }

    if (argc <= 1) {
        help(argv);
        return 1;
    }

    if (optind != argc - 0) {
        cerr << "[rgfa-split] error: too many arguments" << endl;
        help(argv);
        return 1;
    }

    if (selection_options > 1) {
        cerr << "[rgfa-split] error: -q, -c and -C are mutually exclusive" << endl;
        return 1;
    }    

    if (rgfa_path == "-") {
        cerr << "[rgfa-split] error: - (stdin) not supported for rgfa" << endl;
        return 1;
    };

    if (rgfa_path.empty() == input_contig_map_path.empty()) {
        cerr << "[rgfa-split] error: exactly 1 of -g or -m required to specifiy input contigs" << endl;
        return 1;
    }

    function<void(const string&)> check_ifile = [&](const string& path) {
        if (path != "-") {
            ifstream in_stream(path);
            if (!in_stream) {
                cerr << "[rgfa-split] error: unable to open input file \"" << path << "\"" << endl;
                exit(1);
            }
        }
    };
    
    // get the parittion of GFA nodes -> reference contig
    pair<unordered_map<int64_t, int64_t>, vector<string>> partition;
    if (!rgfa_path.empty()) {
        // compute from minigraph output
        check_ifile(rgfa_path);
        partition = rgfa2contig(rgfa_path);
    } else {
        // load table
        check_ifile(input_contig_map_path);
        partition = load_contig_map(input_contig_map_path);
    }

    // output the contig map if path given
    if (!output_contig_map_path.empty()) {
        ofstream output_contig_map_file(output_contig_map_path);
        if (!output_contig_map_file) {
            cerr << "[rgfa-split] error: unable to open output contig map file \"" << output_contig_map_path << "\"" << endl;
            return 1;
        }
        for (auto& node_contig : partition.first) {
            output_contig_map_file << "S" << node_contig.first << "\t" << partition.second[node_contig.second] << "\n";
        }
    }

    // load up contig selection options 
    function<bool(const string&)> visit_contig = [&](const string& name) -> bool {
        if (!contig_names.empty()) {
            return contig_names.count(name);
        } else if (!contig_prefix.empty()) {
            return name.substr(0, contig_prefix.length()) == contig_prefix;
        } else {
            return true;
        }
    };

    // split the paf into one paf per reference contig.  also output a list of query contig names
    // alongside than can be used to split the fasta with samtools faidx
    if (!input_paf_path.empty()) {
        ifstream input_paf_file;
        istream* input_paf_stream;
        if (input_paf_path == "-") {
            input_paf_stream = &cin;
        } else {
            check_ifile(input_paf_path);
            input_paf_file.open(input_paf_path);
            input_paf_stream = &input_paf_file;
        }

        paf_split(*input_paf_stream, partition.first, partition.second, visit_contig, output_prefix);
    }

    return 0;
}
