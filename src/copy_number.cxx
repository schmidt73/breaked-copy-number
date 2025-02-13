#include "copy_number.hpp"
#include "vec_utilities.hpp"

#include <cstdlib>
#include <set>
#include <random>
#include <vector>
#include <map>
#include <ostream>
#include <limits>

namespace copynumber {
    namespace {
        int rand_int(std::ranlux48_base& gen, int a, int b) {
            std::uniform_int_distribution<int> distrib(a, b);
            return distrib(gen);
        }
    }

    std::ostream& operator<<(std::ostream& os, const genomic_bin& bin) {
        os << bin.chromosome << ":" << bin.allele << ":" << bin.start << "-" << bin.end;
        return os;
    }

    breakpoint_profile convert_to_breakpoint_profile(const copynumber_profile &p, int diploid_cn) {
        std::map<std::pair<std::string, std::string>, copynumber_profile> chrom_allele_profiles;
        for (size_t i = 0; i < p.bins.size(); i++) {
            auto chrom_allele = std::make_pair(p.bins[i].chromosome, p.bins[i].allele);
            if (!chrom_allele_profiles.count(chrom_allele)) {
                chrom_allele_profiles[chrom_allele] = copynumber_profile();
            }

            chrom_allele_profiles[chrom_allele].bins.push_back(p.bins[i]);
            chrom_allele_profiles[chrom_allele].profile.push_back(p.profile[i]);
        }

        breakpoint_profile bp;
        for (const auto &[chrom_allele, cn_profile] : chrom_allele_profiles) {
            std::vector<size_t> index_vector = argsort(cn_profile.bins);
            std::vector<genomic_bin> bins = select(cn_profile.bins, index_vector);
            std::vector<int> profile = select(cn_profile.profile, index_vector);

            std::vector<int> bp_profile(profile.size());
            for (size_t i = 0; i < profile.size(); i++) {
                if (i == 0) {
                    bp_profile[i] = profile[i] - diploid_cn;
                } else if (i == profile.size() - 1) {
                    bp_profile[i] = diploid_cn - profile[i];
                } else {
                    bp_profile[i] = profile[i] - profile[i - 1];
                }

                bp.profile.push_back(bp_profile[i]);
                bp.bins.push_back(bins[i]);
            }
        }

        return bp;
    }

    copynumber_profile convert_to_copynumber_profile(const breakpoint_profile &p, int diploid_cn) {
        std::map<std::pair<std::string, std::string>, breakpoint_profile> chrom_allele_profiles;
        for (size_t i = 0; i < p.bins.size(); i++) {
            auto chrom_allele = std::make_pair(p.bins[i].chromosome, p.bins[i].allele);
            if (!chrom_allele_profiles.count(chrom_allele)) {
                chrom_allele_profiles[chrom_allele] = breakpoint_profile();
            }

            chrom_allele_profiles[chrom_allele].bins.push_back(p.bins[i]);
            chrom_allele_profiles[chrom_allele].profile.push_back(p.profile[i]);
        }

        copynumber_profile cn;
        for (const auto &[chrom_allele, bp_profile] : chrom_allele_profiles) {
            std::vector<size_t> index_vector = argsort(bp_profile.bins);
            std::vector<genomic_bin> bins = select(bp_profile.bins, index_vector);
            std::vector<int> profile = select(bp_profile.profile, index_vector);

            std::vector<int> cn_profile(profile.size());
            for (size_t i = 0; i < profile.size(); i++) {
                if (i == 0) {
                    cn_profile[i] = profile[i] + diploid_cn;
                } else if (i == profile.size() - 1) {
                    cn_profile[i] = diploid_cn - profile[i];
                } else {
                    cn_profile[i] = profile[i] + cn_profile[i - 1];
                }

                cn.profile.push_back(cn_profile[i]);
                cn.bins.push_back(bins[i]);
            }
        }

        return cn;
    }
    /*
      Computes the breakpoint magnitude of a *chromosome and allele
      sorted* breakpoint profile.
     */
    int breakpoint_magnitude(const breakpoint_profile& p) {
        if (p.bins.size() < 1) return 0;

        int mag = 0;
        for (std::vector<int>::size_type i = 0; i < p.profile.size(); i++) {
            if (p.profile[i] > 0) {
                mag += p.profile[i];
            } else {
                mag += -p.profile[i];
            }
        }

        return mag;
    }

    breakpoint_profile convert_to_breakpoint_profile(const copynumber_profile &p, int diploid_cn);
    std::optional<std::pair<int, int>> overlap(int s1, int e1, int s2, int e2) {
        std::optional<std::pair<int, int>> out_interval;
    
        if (s1 <= e2 && s1 >= s2) {
            out_interval = std::make_pair(s1, std::min(e1, e2));
        } else if (s2 <= e1 and s2 >= s1) {
            out_interval = std::make_pair(s2, std::min(e1, e2));
        }

        return out_interval;
    }

    std::tuple<std::vector<int>, std::vector<int>, int> sankoff(const rectilinear_vertex_data& u, const rectilinear_vertex_data& v) {
        const std::vector<int> &u_start = u.start.value();
        const std::vector<int> &u_end = u.end.value();
        const std::vector<int> &v_start = v.start.value();
        const std::vector<int> &v_end = v.end.value();

        std::vector<int> start(u_start.size());
        std::vector<int> end(u_end.size());
        int distance = 0;
        for (size_t i = 0; i < u_start.size(); i++) {
            std::optional<std::pair<int, int>> interval = overlap(u_start[i], u_end[i], v_start[i], v_end[i]);

            if (interval) {
                auto [s, e] = interval.value();
                start[i] = s;
                end[i] = e;
            } else {
                if (u_start[i] < v_start[i]) {
                    start[i] = u_end[i];
                    end[i] = v_start[i];
                } else {
                    start[i] = v_end[i];
                    end[i] = u_start[i];
                }

                distance += end[i] - start[i];
            }
        }

        return std::make_tuple(start, end, distance);
    }


    /*
      Returns the optimal labeling of the child given the parent
      labeling and the set of optimal labelings for the child.
     */
    std::vector<int> local_labeling(const std::vector<int>& parent_labeling,
                                    const std::vector<int>& child_start,
                                    const std::vector<int>& child_end) {
        std::vector<int> child_labeling(parent_labeling.size());
        for (size_t i = 0; i < child_labeling.size(); i++) {
            // parent labeling overlaps with optimal child labelings
            if (child_start[i] <= parent_labeling[i] && parent_labeling[i] <= child_end[i]) {
                child_labeling[i] = parent_labeling[i];
            } else {
                int dist1 = abs(parent_labeling[i] - child_start[i]);
                int dist2 = abs(parent_labeling[i] - child_end[i]);
                if (dist1 < dist2) {
                    child_labeling[i] = child_start[i];
                } else {
                    child_labeling[i] = child_end[i];
                }
            }
        }

        return child_labeling;
    }

    digraph<breakpoint_profile_vertex_data> ancestral_labeling(digraph<rectilinear_vertex_data>& t,
                                                               int root,
                                                               std::vector<genomic_bin> bins) {
        std::stack<std::tuple<int, int>> callstack;
        digraph<breakpoint_profile_vertex_data> bt;

        callstack.push(std::make_tuple(root, -1));
        while (!callstack.empty()) {
            auto [node, parent] = callstack.top();
            callstack.pop();

            breakpoint_profile_vertex_data d;
            d.name = t[node].data.name; // copy node name

            if (parent == -1) {
                breakpoint_profile p;
                p.bins = bins;
                p.profile = t[node].data.start.value();
                d.profile = p;
            } else {
                breakpoint_profile p;
                p.bins = bins;
                p.profile = local_labeling(bt[parent].data.profile.profile, t[node].data.start.value(), t[node].data.end.value());
                d.profile = p;
                d.in_branch_length = breakpoint_magnitude(p - bt[parent].data.profile);
            }

            int new_node = bt.add_vertex(d);
            if (parent != -1) {
                bt.add_edge(parent, new_node);
            }

            for (const auto& child : t.successors(node)) {
                callstack.push(std::make_tuple(child, new_node));
            }
        }

        return bt;
    }

    void small_rectilinear(digraph<rectilinear_vertex_data>& t, int root) {
        std::stack<int> callstack;

        callstack.push(root);
        while (!callstack.empty()) {
            int node = callstack.top();
            callstack.pop();

            if (t.out_degree(node) == 0) {
                t[node].data.visited = true;
                continue;
            }

            // check condition that every node has two children
            if (t.out_degree(node) != 2)
                throw std::logic_error("every child must have exactly two children");

            // check to see if all children are visited
            bool children_visited = true; 
            for (const auto& child : t.successors(node)) {
                if (!t[child].data.visited) children_visited = false;
            }

            if (children_visited) {
                // grab two children
                std::set<int> children_s = t.successors(node);
                std::vector<int> children_a(children_s.begin(), children_s.end());

                /* could wrap this into "Sankoff" sub-routine */
                int u = children_a[0];
                int v = children_a[1];

                const rectilinear_vertex_data& u_data = t[u].data;
                const rectilinear_vertex_data& v_data = t[v].data;

                const auto& [start, end, cost] = sankoff(u_data, v_data);

                t[node].data.score = cost + u_data.score + v_data.score;
                t[node].data.start = start;
                t[node].data.end = end;
                t[node].data.visited = true;

                continue;
            }

            callstack.push(node);
            for (const auto& child : t.successors(node)) {
                if (!t[child].data.visited) callstack.push(child);
            }
        }
    }

    void nni(digraph<rectilinear_vertex_data>& t, int u, int w, int v, int z) {
        t.remove_edge(u, w);
        t.remove_edge(v, z);
        t.add_edge(v, w);
        t.add_edge(u, z);
    }

    void undo_nni(digraph<rectilinear_vertex_data>& t, int u, int w, int v, int z) {
        t.add_edge(u, w);
        t.add_edge(v, z);
        t.remove_edge(v, w);
        t.remove_edge(u, z);
    }

    void unvisit(digraph<rectilinear_vertex_data> &t, int root, int u) {
        int current_node = u;
        do {
            t[current_node].data.visited = false;
            current_node = *t.predecessors(current_node).begin(); // requires only one parent exists, i.e. t is a tree
        } while (current_node != root);
    }

    void unvisit(digraph<rectilinear_vertex_data> &t, int root) {
        std::stack<int> callstack;
        callstack.push(root);
        while (!callstack.empty()) {
            int node = callstack.top();
            callstack.pop();

            t[node].data.visited = false;
            for (const auto& child : t.successors(node)) {
                callstack.push(child);
            }
        }
    }

    /*
      Performs all NNIs in the immediate neighborhood of the passed in
      tree and returns the best move. Does not modify the input tree.
      
      Requires:
        - t satisfies the *rectilinear invariant*.
     */
    std::optional<std::tuple<int, int, int, int>> greedy_nni(digraph<rectilinear_vertex_data> &t, 
                                                             const std::map<int, std::pair<int, int>> &indexed_edges,
                                                             const std::vector<int> &edge_indices,
                                                             bool greedy) {
        int best_score = t[0].data.score; // i.e. best_score = \infty
        std::optional<std::tuple<int, int, int, int>> best_move;
        for (int idx : edge_indices) {
            const auto& [u, v] = indexed_edges.at(idx);

            if (t.successors(v).empty()) continue; // i.e if not internal

            std::vector<int> u_children;
            std::vector<int> v_children;

            for (int w : t.successors(u)) {
                if (w != v) {
                    u_children.push_back(w);
                }
            }

            for (int w : t.successors(v)) {
                v_children.push_back(w);
            }


            for (auto w : u_children) {
                for (auto z : v_children) {
                    nni(t, u, w, v, z);
                    unvisit(t, 0, v);
                    small_rectilinear(t, 0);

                    int score = t[0].data.score;
                    if (score < best_score) {
                        best_score = score;
                        best_move = std::make_tuple(u, w, v, z);

                        if (greedy) {
                            undo_nni(t, u, w, v, z);
                            unvisit(t, 0, v);
                            return best_move;
                        }
                    }

                    undo_nni(t, u, w, v, z);
                    unvisit(t, 0, v);

                }
            }
        }

        return best_move;
    }

    digraph<rectilinear_vertex_data> hill_climb(digraph<rectilinear_vertex_data> t, std::ranlux48_base& gen, bool greedy) {
        std::map<int, std::pair<int, int>> index_to_edges;
        std::map<std::pair<int, int>, int> edges_to_index;
        std::vector<int> random_indices;
        
        int idx = 0;
        for (const auto &p : t.edges()) {
            index_to_edges[idx] = p;
            edges_to_index[p] = idx;
            random_indices.push_back(idx);
            idx++;
        }
            
        std::shuffle(random_indices.begin(), random_indices.end(), gen);

        int current_score = t[0].data.score;
        int iterations = 0;
        for (; true; iterations++) {
            auto best_move = greedy_nni(t, index_to_edges, random_indices, greedy);
            if (!best_move) break;

            auto [u, w, v, z] = *best_move;
            nni(t, u, w, v, z);

            // update edge map by deleting (u, w) and (v, z) and adding
            // (v, w) and (u, z)
            int i1 = edges_to_index[std::make_pair(u, w)];
            int i2 = edges_to_index[std::make_pair(v, z)];
            index_to_edges[i1] = std::make_pair(v, w);
            edges_to_index[std::make_pair(v, w)] = i1;
            index_to_edges[i2] = std::make_pair(u, z);
            edges_to_index[std::make_pair(u, z)] = i2;

            unvisit(t, 0, v);
            small_rectilinear(t, 0);
            int new_score = t[0].data.score;

            if (current_score <= new_score) break;
            current_score = new_score;
        }

        return t;
    }

    digraph<rectilinear_vertex_data> stochastic_nni(const digraph<rectilinear_vertex_data>& t, std::ranlux48_base& gen, float aggression) {
        digraph<rectilinear_vertex_data> perturbed_t = t;

        // TODO: investigate performance gain from
        // not recomputing internal edges at every iteration
        std::vector<std::pair<int, int>> internal_edges;
        for (auto [u, v] : perturbed_t.edges()) {
            if (perturbed_t.successors(v).empty()) continue;
            internal_edges.push_back(std::make_pair(u, v));
        }

        int num_perturbations = internal_edges.size() * aggression;
        for (int i = 0; i < num_perturbations; i++) {
            internal_edges.clear();
            for (auto [u, v] : perturbed_t.edges()) {
                if (perturbed_t.successors(v).empty()) continue;
                internal_edges.push_back(std::make_pair(u, v));
            }

            int index = rand_int(gen, 0, internal_edges.size() - 1);
            auto [u, v] = internal_edges[index];

            std::vector<int> u_children;
            std::vector<int> v_children;

            for (int w : perturbed_t.successors(u)) {
                if (w != v) {
                    u_children.push_back(w);
                }
            }

            for (int w : perturbed_t.successors(v)) {
                v_children.push_back(w);
            }

            int w = u_children[rand_int(gen, 0, u_children.size() - 1)];
            int z = v_children[rand_int(gen, 0, v_children.size() - 1)];

            nni(perturbed_t, u, w, v, z);
        }

        unvisit(perturbed_t, 0);
        return perturbed_t;
    }
};
