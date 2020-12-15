#include <map>
#include <list>
#include <cassert>
#include "rgfa-split.hpp"
#include "gfakluge.hpp"
#include "pafcoverage.hpp"

/*

sort nodes by rank (ascending)

index all edges

for each node in sorted list
 if rank == 0
   node.contig = contig from gfa optional element
 else:
   scan all edges for rank i-1 adjacencies
   build consensus contig
*/
pair<unordered_map<int64_t, int64_t>, vector<string>> rgfa2contig(const string& gfa_path) {

    // map rank -> nodes
    map<int64_t, list<int64_t>> rank_to_nodes;

    // map node -> rank
    unordered_map<int64_t, int64_t> node_to_rank;

    // store edges (don't care about orientation, just connectivity)
    unordered_multimap<int64_t, int64_t> edges;

    // output contig ids
    vector<string> contigs;
    // used to compute above
    unordered_map<string, int64_t> contig_map;

    // output map of node id to contig id
    unordered_map<int64_t, int64_t> node_to_contig;

    // get the node ranks, and rank-0 contigs
    function<void(const gfak::sequence_elem&)> visit_seq = [&](const gfak::sequence_elem& gfa_seq) {
        int64_t gfa_id = node_id(gfa_seq.name);
        bool found_SN = false;
        bool found_SR = false;
        int64_t rank;
        string contig;
        // parse the SN (contig) and SR (rank) optional tags
        for (const gfak::opt_elem& oe : gfa_seq.opt_fields) {
            if (oe.key == "SN") {
                assert(found_SN == false);
                contig = oe.val;
                found_SN = true;
            } else if (oe.key == "SR") {
                assert(found_SR == false);
                rank = stol(oe.val);
                assert(rank >= 0);                               
                found_SR = true;
            }
        }
        assert(found_SN);
        assert(found_SR);                       
        // remember the rank
        rank_to_nodes[rank].push_back(gfa_id);
        node_to_rank[gfa_id] = rank;
        // remember the contig (if rank 0)
        if (rank == 0) {
            // get the contig id
            int64_t contig_id;
            if (contig_map.count(contig)) {
                contig_id = contig_map[contig];
            } else {
                contig_id = contig_map.size();
                contig_map[contig] = contig_id;
                contigs.push_back(contig);
            }
            node_to_contig[gfa_id] = contig_id;
        }
    };

    // get the edges
    function<void(const gfak::edge_elem&)> visit_edge = [&](const gfak::edge_elem& gfa_edge) {
        int64_t source_id = node_id(gfa_edge.source_name);
        int64_t sink_id = node_id(gfa_edge.sink_name);
        edges.insert(make_pair(source_id, sink_id));
        edges.insert(make_pair(sink_id, source_id));
    };

    // load the GFA into memory
    gfak::GFAKluge kluge;
    kluge.for_each_sequence_line_in_file(gfa_path.c_str(), visit_seq);
    kluge.for_each_edge_line_in_file(gfa_path.c_str(), visit_edge);

    // fill out the contigs by rank
    for (auto& rank_nodes : rank_to_nodes) {
        // rank 0 was added above
        if (rank_nodes.first > 0) {
            const int64_t& rank = rank_nodes.first;
            auto& nodes_at_rank = rank_nodes.second;
            // todo: clean up (this was a mistake in original design where i forgot that not every rank i node
            // connects to rank i-1)
            int64_t consecutive_pushes = 0;
            while (!nodes_at_rank.empty()) {
                int64_t node_id = nodes_at_rank.back();
                nodes_at_rank.pop_back();
                
                // contig_id -> count of times it's connected
                unordered_map<int64_t, int64_t> counts;
                auto edge_iterators = edges.equal_range(node_id);
                for (auto e = edge_iterators.first; e != edge_iterators.second; ++e) {
                    int64_t& other_id = e->second;
                    int64_t other_rank = node_to_rank[other_id];
                    if (other_rank < rank ||
                        (other_rank == rank && node_to_contig.count(other_id))) {
                        int64_t other_contig = node_to_contig[other_id];
                        ++counts[other_contig];
                    }
                }
                if (counts.size() == 0) {
                    // this node isn't connected to any nodes with rank -1, try it later
                    nodes_at_rank.push_front(node_id);
                    ++consecutive_pushes;
                    if (consecutive_pushes > nodes_at_rank.size()) {
                        cerr << "[error] Unable to assign contigs for the following nodes at rank " << rank << ":\n";
                        for (const auto& ni : nodes_at_rank) {
                            cerr << ni << endl;
                        }
                        exit(1);
                    }
                } else if (counts.size() > 1) {
                    cerr << "[error] Conflict found for node \"" << node_id << "\" with rank \"" << rank << ":\n";
                    for (auto& count_elem : counts) {
                        // we could use a heuristic to resolve. but do not expect this from minigraph output
                        cerr << "\tcontig=" << contigs[count_elem.first] << " count=" << count_elem.second << endl;
                    }
                    exit(1);                    
                } else {
                    assert(counts.size() == 1);
                    // set the contig to the unambiguous neighbour
                    node_to_contig[node_id] = counts.begin()->first;
                    consecutive_pushes = 0;
                }
            }
        }
    }

    return make_pair(node_to_contig, contigs);
}


pair<unordered_map<int64_t, int64_t>, vector<string>> load_contig_map(const string& contgs_path) {
    unordered_map<int64_t, int64_t> contig_map;
    vector<string> contigs;
    assert(false);
    return make_pair(contig_map, contigs);
}

/**
 * Use contigs identified above to split PAF
 */
void paf_split(istream& input_paf_stream,
               const unordered_map<int64_t, int64_t>& contig_map,
               const vector<string>& contigs,
               function<bool(const string&)> visit_contig,
               const string& output_prefix) {

    // note: we're assuming a small number of reference contigs (ie 23), so we can afford to open file
    // for each. 
    unordered_map<int64_t, ofstream*> out_files;

    // load up the query contigs for downstream fasta splitting
    unordered_map<int64_t, unordered_set<string> > query_map;

    string paf_line;
    while (getline(input_paf_stream, paf_line)) {
        vector<string> toks;
        split_delims(paf_line, "\t\n", toks);

        // parse the gaf columns
        string& query_name = toks[0];
        string& target_name = toks[5];

        // use the map to go from the target name (rgfa node id in this case) to t
        // the reference contig (ex chr20)
        int64_t target_id = node_id(target_name);
        assert(contig_map.count(target_id));
        int64_t reference_id = contig_map.at(target_id);
        const string& reference_contig = contigs[reference_id];

        // do we want to visit the contig
        if (visit_contig(reference_contig)) {
            ofstream*& out_paf_stream = out_files[reference_id];
            if (out_paf_stream == nullptr) {
                string out_paf_path = output_prefix + reference_contig + ".paf";
                out_paf_stream = new ofstream(out_paf_path);
                assert(out_files.size() < 100);
                if (!(*out_paf_stream)) {
                    cerr << "error: unable to open output paf file: " << out_paf_path << endl;
                    exit(1);
                }
            }
            *out_paf_stream << paf_line;
            // remember this query contig for future fasta splitting
            query_map[reference_id].insert(query_name);
        } 
        
    }

    // clean up the files
    for (auto& ref_stream : out_files) {
        delete ref_stream.second;
    }
    out_files.clear();

    // write the query_contigs
    for (auto& ref_queries : query_map) {
        const string& reference_contig = contigs[ref_queries.first];
        string out_contigs_path = output_prefix + reference_contig + ".fa_contigs";
        ofstream out_contigs_stream(out_contigs_path);
        if (!out_contigs_stream) {
            cerr << "error: unable to open output contigs path: " << out_contigs_path << endl;
            exit(1);
        }
        for (const string& query_name : ref_queries.second) {
            out_contigs_stream << query_name << "\n";
        }
    }
}

void gfa_split(const string& rgfa_path,
               const unordered_map<int64_t, int64_t>& contig_map,
               const vector<string>& contigs,
               function<bool(const string&)> visit_contig,
               const string& output__prefix) {

    ifstream input_gfa_stream(rgfa_path);
    assert(input_gfa_stream);

    // note: we're assuming a small number of reference contigs (ie 23), so we can afford to open file
    // for each. 
    unordered_map<int64_t, ofstream*> out_files;

    string gfa_line;
    while (getline(input_gfa_stream, gfa_line)) {
        vector<string> toks;
        split_delims(gfa_line, "\t\n", toks);

        const string* ref_contig = nullptr;
        int64_t reference_id;
        if (toks[0] == "S") {
            int64_t seq_id = node_id(toks[1]);
            assert(contig_map.count(seq_id));
            reference_id = contig_map.at(seq_id);
            ref_contig = &contigs[reference_id];
        } else if (toks[0] == "L") {
            int64_t seq_id = node_id(toks[1]);
            assert(contig_map.count(seq_id));            
            reference_id = contig_map.at(seq_id);
            int64_t sink_seq_id = node_id(toks[3]);
            assert(contig_map.count(sink_seq_id));            
            int64_t sink_reference_id = contig_map.at(sink_seq_id);
            assert(sink_reference_id == reference_id);
            ref_contig = &contigs[reference_id];
        }
        if (ref_contig != nullptr && visit_contig(*ref_contig)) {
            ofstream*& out_gfa_stream = out_files[reference_id];
            if (out_gfa_stream == nullptr) {
                string out_gfa_path = output__prefix + *ref_contig + ".gfa";
                out_gfa_stream = new ofstream(out_gfa_path);
                assert(out_files.size() < 100);
                if (!(*out_gfa_stream)) {
                    cerr << "error: unable to open output gfa file: " << out_gfa_path << endl;
                    exit(1);
                }
            }
            *out_gfa_stream << gfa_line;
        }
    }

    // clean up the files
    for (auto& ref_stream : out_files) {
        delete ref_stream.second;
    }
    out_files.clear();
}
