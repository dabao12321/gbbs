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

#include "bridge.h"
#include "flags.h"
#include "macros.h"

#include <functional>
#include <limits>
#include <optional>

namespace gbbs {

template <class data>
struct vertexSubsetData {
  using S = std::tuple<uintE, data>;
  using D = std::tuple<bool, data>;

  template <class T>
  using sequence = pbbs::sequence<T>;

  // Move constructor
  vertexSubsetData<data>(vertexSubsetData<data>&& other) noexcept {
    n = other.n;
    m = other.m;
    s = std::move(other.s);
    d = std::move(other.d);
    isDense = other.isDense;
    sum_out_degrees = other.sum_out_degrees;
  }

  // Move assignment
  vertexSubsetData<data>& operator=(vertexSubsetData<data>&& other) noexcept {
    if (this != &other) {
      n = other.n;
      m = other.m;
      s = std::move(other.s);
      d = std::move(other.d);
      isDense = other.isDense;
      sum_out_degrees = other.sum_out_degrees;
    }
    return *this;
  }

  // An empty vertex set.
  vertexSubsetData(size_t _n)
      : n(_n),
        m(0),
        isDense(0),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  // A vertexSubset from array of vertex indices.
  vertexSubsetData(size_t _n, size_t _m, sequence<S>&& A)
      : n(_n),
        m(_m),
        s(std::move(A)),
        isDense(0),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  // A vertexSubset from array of vertex indices.
  vertexSubsetData(size_t _n, sequence<S>&& A)
      : n(_n),
        m(A.size()),
        s(std::move(A)),
        isDense(0),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  // A vertexSubset from boolean array giving number of true values.
  vertexSubsetData(size_t _n, size_t _m, sequence<D>&& A)
      : n(_n),
        m(_m),
        d(std::move(A)),
        isDense(1),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  // A vertexSubset from boolean array giving number of true values. Calculate
  // number of nonzeros and store in m.
  vertexSubsetData(size_t _n, sequence<D>&& A)
      : n(_n),
        d(std::move(A)),
        isDense(1),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {
    auto df = [&](size_t i) { return (size_t)std::get<0>(d[i]); };
    auto d_map = pbbslib::make_sequence<size_t>(n, df);
    m = pbbslib::reduce_add(d_map);
  }

  vertexSubsetData()
      : n(0),
        m(0),
        isDense(0),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  bool out_degrees_set() {
    return (sum_out_degrees != std::numeric_limits<size_t>::max());
  }
  size_t get_out_degrees() { return sum_out_degrees; }
  void set_out_degrees(size_t _sum_out_degrees) {
    sum_out_degrees = _sum_out_degrees;
  }

  // Sparse
  inline uintE& vtx(const uintE& i) { return std::get<0>(s[i]); }
  inline data& vtxData(const uintE& i) { return std::get<1>(s[i]); }
  inline std::tuple<uintE, data> vtxAndData(const uintE& i) const {
    return s[i];
  }

  // Dense
  __attribute__((always_inline)) inline bool isIn(const uintE& v) const {
    return std::get<0>(d[v]);
  }
  inline data& ithData(const uintE& v) { return std::get<1>(d[v]); }

  // Returns (uintE) -> std::optional<std::tuple<vertex, vertex-data>>.
  auto get_fn_repr() const
      -> std::function<std::optional<std::tuple<uintE, data>>(uintE)> {
    std::function<std::optional<std::tuple<uintE, data>>(const uintE&)> fn;
    if (isDense) {
      fn = [&](const uintE& v) -> std::optional<std::tuple<uintE, data>> {
        const auto& dv = d[v];
        if (std::get<0>(dv)) {
          return std::optional<std::tuple<uintE, data>>(
              std::make_tuple(v, std::get<1>(d[v])));
        } else {
          return std::nullopt;
        }
      };
    } else {
      fn = [&](const uintE& i) -> std::optional<std::tuple<uintE, data>> {
        return std::optional<std::tuple<uintE, data>>(s[i]);
      };
    }
    return fn;
  }

  size_t size() const { return m; }
  size_t numVertices() const { return n; }

  size_t numRows() const { return n; }
  size_t numNonzeros() const { return m; }

  bool isEmpty() const { return m == 0; }
  bool dense() const { return isDense; }

  void toSparse() {
    if (s.size() == 0 && m > 0) {
      auto f = [&](size_t i) -> std::tuple<bool, data> { return d[i]; };
      auto f_seq = pbbslib::make_sequence<D>(n, f);
      s = pbbslib::pack_index_and_data<uintE, data>(f_seq, n);
      if (s.size() != m) {
        std::cout << "# m is " << m << " but out.size says" << s.size()
                  << std::endl;
        std::cout << "# bad stored value of m"
                  << "\n";
        abort();
      }
    }
    isDense = false;
  }

  // Convert to dense but keep sparse representation if it exists.
  void toDense() {
    if (d.size() == 0) {
      d = sequence<D>(n);
      par_for(0, n, [&](size_t i) { std::get<0>(d[i]) = false; });
      par_for(0, m, [&](size_t i) {
        d[std::get<0>(s[i])] = std::make_tuple(true, std::get<1>(s[i]));
      });
    }
    isDense = true;
  }

  size_t n, m;
  sequence<S> s;
  sequence<D> d;
  bool isDense;
  size_t sum_out_degrees;
};

// Specialized version where data = pbbslib::empty.
template <>
struct vertexSubsetData<pbbslib::empty> {
  using S = uintE;
  using D = bool;

  // Move constructor
  vertexSubsetData<pbbslib::empty>(vertexSubsetData<pbbslib::empty>&& other) noexcept {
    n = other.n;
    m = other.m;
    s = std::move(other.s);
    d = std::move(other.d);
    isDense = other.isDense;
    sum_out_degrees = other.sum_out_degrees;
  }

  // Move assignment
  vertexSubsetData<pbbslib::empty>& operator=(vertexSubsetData<pbbslib::empty>&& other) noexcept {
    if (this != &other) {
      n = other.n;
      m = other.m;
      s = std::move(other.s);
      d = std::move(other.d);
      isDense = other.isDense;
      sum_out_degrees = other.sum_out_degrees;
    }
    return *this;
  }

  // An empty vertex set.
  vertexSubsetData<pbbslib::empty>(size_t _n)
      : n(_n),
        m(0),
        isDense(0),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  // A vertexSubset with a single vertex.
  vertexSubsetData<pbbslib::empty>(size_t _n, uintE v)
      : n(_n),
        m(1),
        isDense(0),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {
    s = sequence<uintE>(1);
    s[0] = v;
  }

  // A vertexSubset from array of vertex indices.
  vertexSubsetData<pbbslib::empty>(size_t _n, size_t _m, sequence<S>&& A)
      : n(_n),
        m(_m),
        s(std::move(A)),
        isDense(0),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  vertexSubsetData<pbbslib::empty>(size_t n, sequence<S>&& A)
      : n(n),
        m(A.size()),
        s(std::move(A)),
        isDense(0),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  // A vertexSubset from boolean array giving number of true values.
  vertexSubsetData<pbbslib::empty>(size_t _n, size_t _m, sequence<D>&& A)
      : n(_n),
        m(_m),
        d(std::move(A)),
        isDense(1),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {}

  // A vertexSubset from boolean array giving number of true values. Calculate
  // number of nonzeros and store in m.
  vertexSubsetData<pbbslib::empty>(size_t _n, sequence<D>&& A)
      : n(_n),
        d(std::move(A)),
        isDense(1),
        sum_out_degrees(std::numeric_limits<size_t>::max()) {
    auto d_f = [&](size_t i) { return d[i]; };
    auto d_map = pbbslib::make_sequence<size_t>(n, d_f);
    m = pbbslib::reduce_add(d_map);
  }

  bool out_degrees_set() {
    return (sum_out_degrees != std::numeric_limits<size_t>::max());
  }
  size_t get_out_degrees() { return sum_out_degrees; }
  void set_out_degrees(size_t _sum_out_degrees) {
    sum_out_degrees = _sum_out_degrees;
  }

  // Sparse
  inline uintE& vtx(const uintE& i) { return s[i]; }
  inline pbbslib::empty vtxData(const uintE& i) {
    return pbbslib::empty();
  }
  inline std::tuple<uintE, pbbslib::empty> vtxAndData(const uintE& i) const {
    return std::make_tuple(s[i], pbbslib::empty());
  }

  // Dense
  __attribute__((always_inline)) inline bool isIn(const uintE& v) const {
    return d[v];
  }
  inline pbbslib::empty ithData(const uintE& v) const {
    return pbbslib::empty();
  }

  // Returns (uintE) -> std::optional<std::tuple<vertex, vertex-data>>.
  auto get_fn_repr() const -> std::function<
      std::optional<std::tuple<uintE, pbbslib::empty>>(uintE)> {
    std::function<std::optional<std::tuple<uintE, pbbslib::empty>>(
        const uintE&)>
        fn;
    if (isDense) {
      fn = [&](
          const uintE& v) -> std::optional<std::tuple<uintE, pbbslib::empty>> {
        if (d[v]) {
          return std::optional<std::tuple<uintE, pbbslib::empty>>(
              std::make_tuple(v, pbbslib::empty()));
        } else {
          return std::nullopt;
        }
      };
    } else {
      fn = [&](
          const uintE& i) -> std::optional<std::tuple<uintE, pbbslib::empty>> {
        return std::optional<std::tuple<uintE, pbbslib::empty>>(
            std::make_tuple(s[i], pbbslib::empty()));
      };
    }
    return fn;
  }

  size_t size() const { return m; }
  size_t numVertices() const { return n; }

  size_t numRows() const { return n; }
  size_t numNonzeros() const { return m; }

  bool isEmpty() const { return m == 0; }
  bool dense() const { return isDense; }

  void toSparse() {
    if (s.size() == 0 && m > 0) {
      auto f_in =
          pbbslib::make_sequence<bool>(n, [&](size_t i) { return d[i]; });
      s = pbbslib::pack_index<uintE>(f_in);
      if (s.size() != m) {
        std::cout << "# m is " << m << " but out.size says" << s.size()
                  << std::endl;
        std::cout << "# bad stored value of m"
                  << "\n";
        std::cout << "# out.size = " << s.size() << " m = " << m
                  << " n = " << n << "\n";
        abort();
      }
    }
    isDense = false;
  }

  // Converts to dense but keeps sparse representation if it exists.
  void toDense() {
    if (d.size() == 0) {
      d = sequence<bool>(n);
      par_for(0, n, [&](size_t i) { d[i] = 0; });
      par_for(0, m, [&](size_t i) { d[s[i]] = 1; });
    }
    isDense = true;
  }

  size_t n, m;
  sequence<S> s;
  sequence<D> d;
  bool isDense;
  size_t sum_out_degrees;
};
using vertexSubset = vertexSubsetData<pbbslib::empty>;

/* ======================== Functions on VertexSubsets ====================== */

// Takes a vertexSubsetData (with some non-trivial Data) and applies a map
// function f : (uintE x Data) -> void over each vertex in the vertexSubset, in
// parallel.
template <class F, class VS,
          typename std::enable_if<!std::is_same<VS, vertexSubset>::value,
                                  int>::type = 0>
inline void vertexMap(VS& V, F f,
                      size_t granularity = pbbslib::kSequentialForThreshold) {
  size_t n = V.numRows(), m = V.numNonzeros();
  if (V.dense()) {
    parallel_for(0, n,
                 [&](size_t i) {
                   if (V.isIn(i)) {
                     f(i, V.ithData(i));
                   }
                 },
                 granularity);
  } else {
    parallel_for(0, m, [&](size_t i) { f(V.vtx(i), V.vtxData(i)); },
                 granularity);
  }
}

// Takes a vertexSubset (with no extra data per-vertex) and applies a map
// function f : uintE -> void over each vertex in the vertexSubset, in
// parallel.
template <class VS, class F,
          typename std::enable_if<std::is_same<VS, vertexSubset>::value,
                                  int>::type = 0>
inline void vertexMap(VS& V, F f,
                      size_t granularity = pbbslib::kSequentialForThreshold) {
  size_t n = V.numRows(), m = V.numNonzeros();
  if (V.dense()) {
    parallel_for(0, n,
                 [&](size_t i) {
                   if (V.isIn(i)) {
                     f(i);
                   }
                 },
                 granularity);
  } else {
    parallel_for(0, m, [&](size_t i) { f(V.vtx(i)); }, granularity);
  }
}

template <class F, class Data>
inline vertexSubset vertexFilter_dense(
    vertexSubsetData<Data>& V, F filter,
    size_t granularity = pbbslib::kSequentialForThreshold) {
  size_t n = V.numRows();
  V.toDense();
  auto d_out = sequence<bool>::no_init(n);
  parallel_for(0, n, [&](size_t i) { d_out[i] = 0; }, granularity);
  parallel_for(0, n,
               [&](size_t i) {
                 if
                   constexpr(std::is_same<Data, pbbs::empty>::value) {
                     if (V.isIn(i)) d_out[i] = filter(i);
                   }
                 else {
                   if (V.isIn(i)) d_out[i] = filter(i, V.ithData(i));
                 }
               },
               granularity);
  return vertexSubset(n, std::move(d_out));
}

template <class F, class Data>
inline vertexSubset vertexFilter_sparse(
    vertexSubsetData<Data>& V, F filter,
    size_t granularity = pbbslib::kSequentialForThreshold) {
  size_t n = V.numRows(), m = V.numNonzeros();
  if (m == 0) {
    return vertexSubset(n);
  }
  bool* bits = pbbslib::new_array_no_init<bool>(m);
  V.toSparse();
  parallel_for(0, m,
               [&](size_t i) {
                 uintE v = V.vtx(i);
                 if
                   constexpr(std::is_same<Data, pbbs::empty>::value) {
                     bits[i] = filter(v);
                   }
                 else {
                   bits[i] = filter(v, V.vtxData(i));
                 }
               },
               granularity);
  auto v_imap =
      pbbslib::make_sequence<uintE>(m, [&](size_t i) { return V.vtx(i); });
  auto bits_m =
      pbbslib::make_sequence<bool>(m, [&](size_t i) { return bits[i]; });
  auto out = pbbslib::pack(v_imap, bits_m);
  pbbslib::free_array(bits);
  return vertexSubset(n, std::move(out));
}

// Note that this currently strips vertices of their associated data (which is
// the intended use-case in all current uses). Should refactor at some point to
// make keeping/removing the data a choice.
template <class F, class VS>
inline vertexSubset vertexFilter(VS& vs, F filter, flags fl = 0) {
  if (fl == dense_only) {
    return vertexFilter_dense(vs, filter);
  } else if (fl == no_dense) {
    return vertexFilter_sparse(vs, filter);
  }
  // TODO: can measure selectivity and call sparse/dense based on a sample.
  if (vs.dense()) {
    return vertexFilter_dense(vs, filter);
  }
  return vertexFilter_sparse(vs, filter);
}

void add_to_vsubset(vertexSubset& vs, uintE* new_verts, uintE num_new_verts);

// template <class VS,
//          typename std::enable_if<!std::is_same<VS, vertexSubset>::value,
//                                  int>::type = 0>
// void add_to_vsubset(VS& vs, uintE* new_verts, uintE num_new_verts, size_t
// granulairty=pbbslib::kSequentialForThreshold) {
//  std::cout << "Currently unimplemented" << std::endl;
//  exit(-1);
//}

}  // namespace gbbs
