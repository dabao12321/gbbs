// This code is part of the project "Theoretically Efficient Parallel Graph
// Algorithms Can Be Fast and Scalable", presented at Symposium on Parallelism
// in Algorithms and Architectures, 2018.
// Copyright (c) 2018 Laxman Dhulipala, Guy Blelloch, and Julian Shun
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all  copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "gbbs/gbbs.h"
#include "pbbslib/random_shuffle.h"
#include "benchmarks/SpanningForest/common.h"
#include "benchmarks/Connectivity/connectit.h"

namespace gbbs {
namespace labelprop_sf {

  constexpr uint8_t unemitted = 0;
  constexpr uint8_t need_emit = 1;
  constexpr uint8_t emitted = 2;

  bool lp_less(uintE u, uintE v) {
    if (u == spanning_forest::largest_comp) {
      return (v != spanning_forest::largest_comp);
    } else if (v == spanning_forest::largest_comp) {
      return false;
    }
    return u < v;
  }

  template <class W>
  struct LabelProp_F {
    pbbs::sequence<parent>& PrevParents;
    pbbs::sequence<parent>& Parents;
    pbbs::sequence<uint8_t>& changed;
    LabelProp_F(pbbs::sequence<parent>& PrevParents, pbbs::sequence<parent>& Parents, pbbs::sequence<uint8_t>& changed) : PrevParents(PrevParents), Parents(Parents), changed(changed) {}
    inline bool update(const uintE& s, const uintE& d, const W& w) {
      return updateAtomic(s, d, w);
    }

    inline bool updateAtomic(const uintE& s, const uintE& d, const W& w) {
      if (lp_less(PrevParents[s], PrevParents[d])) {
        pbbs::write_min<uintE>(&Parents[d], PrevParents[s], lp_less);
        return pbbs::write_min(&changed[d], emitted, std::greater<uint8_t>());
      } else if (lp_less(PrevParents[d], PrevParents[s])) {
        if (pbbs::write_min<uintE>(&Parents[s], PrevParents[d], lp_less)) {
          if (changed[s] == unemitted) {
            pbbs::write_min(&changed[s], need_emit, std::greater<uint8_t>());
          }
        }
      }
      return 0;
    }
    inline bool cond(const uintE& d) { return true; }
  };

  template <class W>
  struct LabelProp_F_2 {
    pbbs::sequence<parent>& PrevParents;
    pbbs::sequence<parent>& Parents;
    pbbs::sequence<edge>& Edges;
    LabelProp_F_2(pbbs::sequence<parent>& PrevParents, pbbs::sequence<parent>& Parents, pbbs::sequence<edge>& Edges) : PrevParents(PrevParents), Parents(Parents), Edges(Edges) {}

    inline bool update(const uintE& s, const uintE& d, const W& w) {
      return updateAtomic(s, d, w);
    }

    inline bool updateAtomic(const uintE& s, const uintE& d, const W& w) {
      if (lp_less(PrevParents[s], PrevParents[d])) { // were not equal before this round
        if (Parents[d] == PrevParents[s]) { // ours was the winner
          auto prev_edge = Edges[d];
          pbbs::atomic_compare_and_swap(&Edges[d], prev_edge, std::make_pair(s,d));
        }
      } else if (lp_less(PrevParents[d], PrevParents[s])) { // were not equal before this round
        if (Parents[s] == PrevParents[d]) { // ours was the winner
          auto prev_edge = Edges[s];
          pbbs::atomic_compare_and_swap(&Edges[s], prev_edge, std::make_pair(s,d));
        }
      }
      return 0;
    }
    inline bool cond(const uintE& d) { return true; }
  };


  template <class Graph>
  struct LPAlgorithm {
    Graph& GA;
    LPAlgorithm(Graph& GA) : GA(GA) {}

    void initialize(pbbs::sequence<parent>& P, pbbs::sequence<edge>& E) {}

    template <SamplingOption sampling_option>
    void compute_spanning_forest(pbbs::sequence<parent>& Parents, pbbs::sequence<edge>& Edges, uintE frequent_comp = UINT_E_MAX) {
      using W = typename Graph::weight_type;
      size_t n = GA.n;

      auto vs = vertexSubset(n);
      pbbs::sequence<bool> all;
      if constexpr (sampling_option == no_sampling) {
        all = pbbs::sequence<bool>(n, true);
      } else { /* frequent_comp provided */
        all = pbbs::sequence<bool>(n, [&] (size_t i) -> bool {
          return Parents[i] != frequent_comp;
        });
      }
      vs = vertexSubset(n, std::move(all));
      std::cout << "### initial vs = " << vs.size() << std::endl;

      auto PrevParents = Parents;

      size_t rounds = 0;
      auto changed = pbbs::sequence<uint8_t>(n, (uint8_t)0);
      size_t vertices_processed = 0;
      while (!vs.isEmpty()) {
        std::cout << "### vs size = " << vs.size() << std::endl;
        vertices_processed += vs.size();

        auto next_vs = edgeMap(GA, vs, LabelProp_F<W>(PrevParents, Parents, changed), -1, dense_forward);

        edgeMap(GA, vs, LabelProp_F_2<W>(PrevParents, Parents, Edges), -1, dense_forward | no_output);

        vs.toSparse();
        auto this_vs = pbbs::delayed_seq<uintE>(vs.size(), [&] (size_t i) {
          return vs.vtx(i);
        });
        auto new_vtxs = pbbs::filter(this_vs, [&] (uintE v) {
          return changed[v] == need_emit; /* emit those that need emitting */
        });
        std::cout << "### num acquired through need_emitted = " << new_vtxs.size() << std::endl;
        add_to_vsubset(next_vs, new_vtxs.begin(), new_vtxs.size());

        vs = std::move(next_vs);
        vertexMap(vs, [&] (const uintE& u) {
          PrevParents[u] = Parents[u];
          changed[u] = unemitted;
        });
        rounds++;
      }
      std::cout << "# LabelProp: ran " << rounds << " many rounds." << std::endl;
      std::cout << "# processed " << vertices_processed << " many vertices" << std::endl;
    }
  };

  template <bool use_permutation, class Graph>
  inline sequence<edge> SpanningForest(Graph& G) {
    size_t n = G.n;
    pbbs::sequence<parent> Parents;
    pbbs::sequence<edge> Edges(n, empty_edge);
    if constexpr (use_permutation) {
      Parents = pbbs::random_permutation<uintE>(n);
    } else {
      Parents = pbbs::sequence<parent>(n, [&] (size_t i) { return i; });
    }
    auto alg = LPAlgorithm<Graph>(G);
    alg.template compute_spanning_forest<no_sampling>(Parents, Edges);

    return pbbs::filter(Edges, [&] (const edge& e) {
      return e != empty_edge;
    });
  }

}  // namespace labelprop_sf
}  // namespace gbbs
