/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include "kernel/expr.h"
#include "library/expr_lt.h"
#include "library/type_context.h"
#include "library/congr_lemma.h"
#include "library/relation_manager.h"

namespace lean {
struct ext_congr_lemma;

struct ext_congr_lemma_cache;
typedef std::shared_ptr<ext_congr_lemma_cache> ext_congr_lemma_cache_ptr;

class congruence_closure {
    /* Key for the equality congruence table. */
    struct congr_key {
        expr      m_expr;
        unsigned  m_hash;
    };

    struct congr_key_cmp {
        int operator()(congr_key const & k1, congr_key const & k2) const;
    };

    /* Key for the equality congruence table for symmetric relations.

       Remark: the same congruence table can be used to handle
       commutative operations. */
    struct symm_congr_key {
        expr      m_expr;
        unsigned  m_hash;
        name      m_rel;
    };

    struct symm_congr_key_cmp {
        int operator()(symm_congr_key const & k1, symm_congr_key const & k2) const;
    };

    /* Equivalence class data associated with an expression 'e' */
    struct entry {
        expr           m_next;       // next element in the equivalence class.
        expr           m_root;       // root (aka canonical) representative of the equivalence class.
        expr           m_cg_root;    // root of the congruence class, it is meaningless if 'e' is not an application.
        /* When 'e' was added to this equivalence class because of an equality (H : e = target), then we
           store 'target' at 'm_target', and 'H' at 'm_proof'. Both fields are none if 'e' == m_root */
        optional<expr> m_target;
        optional<expr> m_proof;
        unsigned       m_flipped:1;      // proof has been flipped
        unsigned       m_to_propagate:1; // must be propagated back to state when in equivalence class containing true/false
        unsigned       m_interpreted:1;  // true if the node should be viewed as an abstract value
        unsigned       m_constructor:1;  // true if head symbol is a constructor
        /* m_heq_proofs == true iff some proofs in the equivalence class are based on heterogeneous equality.
           We represent equality and heterogeneous equality in a single equivalence class. */
        unsigned       m_heq_proofs:1;
        /* If m_fo == true, then the expression associated with this entry is an application, and
           we are using first-order approximation to encode it. That is, we ignore its partial applications. */
        unsigned       m_fo:1;
        unsigned       m_size;           // number of elements in the equivalence class, it is meaningless if 'e' != m_root
        /* The field m_mt is used to implement the mod-time optimization introduce by the Simplify theorem prover.
           The basic idea is to introduce a counter gmt that records the number of heuristic instantiation that have
           occurred in the current branch. It is incremented after each round of heuristic instantiation.
           The field m_mt records the last time any proper descendant of of thie entry was involved in a merge. */
        unsigned       m_mt;
    };

    struct parent_occ {
        expr m_expr;
        /* If m_symm_table is true, then we should use the m_symm_congruences, otherwise m_congruences.
           Remark: this information is redundant, it can be inferred from m_expr. We use store it for
           performance reasons. */
        bool m_symm_table;
        parent_occ() {}
        parent_occ(expr const & e, bool symm_table):m_expr(e), m_symm_table(symm_table) {}
    };

    struct parent_occ_cmp {
        int operator()(parent_occ const & k1, parent_occ const & k2) const {
            return expr_quick_cmp()(k1.m_expr, k2.m_expr);
        }
    };

    typedef rb_tree<expr, expr_quick_cmp>                expr_set;
    typedef rb_map<expr, entry, expr_quick_cmp>          entries;
    typedef rb_tree<parent_occ, parent_occ_cmp>          parent_occ_set;
    typedef rb_map<expr, parent_occ_set, expr_quick_cmp> parents;
    typedef rb_tree<congr_key, congr_key_cmp>            congruences;
    typedef rb_tree<symm_congr_key, symm_congr_key_cmp>  symm_congruences;
    typedef rb_map<expr, expr, expr_quick_cmp>           subsingleton_reprs;
    typedef std::tuple<expr, expr, expr, bool>           todo_entry;

public:
    struct config {
        unsigned m_ignore_instances:1;
        unsigned m_values:1;
        unsigned m_all_ho:1;
        config() { m_ignore_instances = true; m_values = true; m_all_ho = false; }
    };

    class state {
        entries            m_entries;
        parents            m_parents;
        congruences        m_congruences;
        symm_congruences   m_symm_congruences;
        /** The following mapping store a representative for each subsingleton type */
        subsingleton_reprs m_subsingleton_reprs;
        /** The congruence closure module has a mode where the root of
            each equivalence class is marked as an interpreted/abstract
            value. Moreover, in this mode proof production is disabled.
            This capability is useful for heuristic instantiation. */
        short              m_froze_partitions{false};
        short              m_inconsistent{false};
        unsigned           m_gmt{0};
        /** Only for constant functions in m_ho_fns, we add the extra occurrences discussed in
            the paper "Congruence Closure in Intensional Type Theory". The idea is to avoid
            the quadratic number of entries in the parent occurrences data-structures,
            and avoid the creation of entries for partial applications. For example, given
            (@add nat nat_has_add a b), it seems wasteful to create entries for
            (@add nat), (@add nat nat_has_add) and (@nat nat_has_add a).
            This set is ignore if m_config.m_all_ho is true. */
        name_set           m_ho_fns;
        config             m_config;
        friend class congruence_closure;
        bool check_eqc(expr const & e) const;
        void mk_entry_core(expr const & e, bool to_propagate, bool interpreted, bool constructor);
    public:
        state(name_set const & ho_fns = name_set(), config const & cfg = config());
        void get_roots(buffer<expr> & roots, bool nonsingleton_only = true) const;
        expr get_root(expr const & e) const;
        expr get_next(expr const & e) const;
        unsigned get_mt(expr const & e) const;
        bool is_congr_root(expr const & e) const;
        bool check_invariant() const;
        bool inconsistent() const { return m_inconsistent; }
        bool in_singleton_eqc(expr const & e) const;
        bool in_heterogeneous_eqc(expr const & e) const;
        format pp_eqc(formatter const & fmt, expr const & e) const;
        format pp_eqcs(formatter const & fmt, bool nonsingleton_only = true) const;
        format pp_parent_occs(formatter const & fmt, expr const & e) const;
        format pp_parent_occs(formatter const & fmt) const;
        unsigned get_gmt() const { return m_gmt; }
    };

private:
    type_context &            m_ctx;
    state &                   m_state;
    buffer<todo_entry>        m_todo;
    ext_congr_lemma_cache_ptr m_cache_ptr;
    transparency_mode         m_mode;
    relation_info_getter      m_rel_info_getter;
    symm_info_getter          m_symm_info_getter;
    refl_info_getter          m_refl_info_getter;

    int compare_symm(expr lhs1, expr rhs1, expr lhs2, expr rhs2) const;
    unsigned symm_hash(expr const & lhs, expr const & rhs) const;
    optional<name> is_binary_relation(expr const & e, expr & lhs, expr & rhs) const;
    optional<name> is_symm_relation(expr const & e, expr & lhs, expr & rhs) const;
    optional<name> is_refl_relation(expr const & e, expr & lhs, expr & rhs) const;
    bool is_symm_relation(expr const & e);
    congr_key mk_congr_key(expr const & e) const;
    symm_congr_key mk_symm_congr_key(expr const & e) const;
    void set_fo(expr const & e);
    bool is_logical_app(expr const & n);
    bool is_value(expr const & e);
    bool is_interpreted_value(expr const & e);
    void process_subsingleton_elem(expr const & e);
    void apply_simple_eqvs(expr const & e);
    void add_occurrence(expr const & parent, expr const & child, bool symm_table);
    void add_congruence_table(expr const & e);
    void check_eq_true(symm_congr_key const & k);
    void add_symm_congruence_table(expr const & e);
    void mk_entry_core(expr const & e, bool to_propagate, bool interpreted = false);
    void mk_entry(expr const & e, bool to_propagate);
    void internalize_core(expr const & e, bool toplevel, bool to_propagate);
    void push_todo(expr const & lhs, expr const & rhs, expr const & H, bool heq_proof);
    void push_refl_eq(expr const & lhs, expr const & rhs);
    void invert_trans(expr const & e, bool new_flipped, optional<expr> new_target, optional<expr> new_proof);
    void invert_trans(expr const & e);
    void remove_parents(expr const & e);
    void reinsert_parents(expr const & e);
    void update_mt(expr const & e);
    bool has_heq_proofs(expr const & root) const;
    expr flip_proof(expr const & H, bool flipped, bool heq_proofs) const;
    optional<ext_congr_lemma> mk_ext_hcongr_lemma(expr const & fn, unsigned nargs) const;
    expr mk_trans(expr const & H1, expr const & H2, bool heq_proofs) const;
    expr mk_trans(optional<expr> const & H1, expr const & H2, bool heq_proofs) const;
    expr mk_congr_proof_core(expr const & e1, expr const & e2, bool heq_proofs) const;
    optional<expr> mk_symm_congr_proof(expr const & e1, expr const & e2, bool heq_proofs) const;
    expr mk_congr_proof(expr const & lhs, expr const & rhs, bool heq_proofs) const;
    expr mk_proof(expr const & lhs, expr const & rhs, expr const & H, bool heq_proofs) const;
    optional<expr> get_eq_proof_core(expr const & e1, expr const & e2, bool as_heq) const;
    void push_subsingleton_eq(expr const & a, expr const & b);
    void check_new_subsingleton_eq(expr const & old_root, expr const & new_root);
    void propagate_constructor_eq(expr const & e1, expr const & e2);
    void propagate_value_inconsistency(expr const & e1, expr const & e2);
    void add_eqv_step(expr e1, expr e2, expr const & H,
                      optional<expr> const & added_prop, bool heq_proof);
    void process_todo(optional<expr> const & added_prop);
    void add_eqv_core(expr const & lhs, expr const & rhs, expr const & H,
                      optional<expr> const & added_prop, bool heq_proof);
    bool check_eqc(expr const & e) const;

    friend ext_congr_lemma_cache_ptr const & get_cache_ptr(congruence_closure const & cc);

public:
    congruence_closure(type_context & ctx, state & s);
    ~congruence_closure();

    environment const & env() const { return m_ctx.env(); }
    type_context & ctx() { return m_ctx; }
    transparency_mode mode() const { return m_mode; }

    /** \brief Register expression \c e in this data-structure.
       It creates entries for each sub-expression in \c e.
       It also updates the m_parents mapping.

       We ignore the following subterms of \c e.
       1- and, or and not-applications are not inserted into the data-structures, but
          their arguments are visited.
       2- (Pi (x : A), B), the subterms A and B are not visited if B depends on x.
       3- (A -> B) is not inserted into the data-structures, but their arguments are visited
          if they are propositions.
       4- Terms containing meta-variables.
       5- The subterms of lambda-expressions.
       6- Sorts (Type and Prop). */
    void internalize(expr const & e, bool toplevel);
    void internalize(expr const & e);

    void add(expr const & type, expr const & proof);

    bool is_eqv(expr const & e1, expr const & e2) const;
    bool is_not_eqv(expr const & e1, expr const & e2) const;
    bool proved(expr const & e) const;

    expr get_root(expr const & e) const { return m_state.get_root(e); }
    expr get_next(expr const & e) const { return m_state.get_next(e); }
    bool is_congr_root(expr const & e) const { return m_state.is_congr_root(e); }
    bool in_heterogeneous_eqc(expr const & e) const { return m_state.in_heterogeneous_eqc(e); }

    optional<expr> get_heq_proof(expr const & e1, expr const & e2) const;
    optional<expr> get_eq_proof(expr const & e1, expr const & e2) const;
    optional<expr> get_proof(expr const & e1, expr const & e2) const;
    optional<expr> get_inconsistency_proof() const;

    unsigned get_gmt() const { return m_state.get_gmt(); }
    unsigned get_mt(expr const & t) const { return m_state.get_mt(t); }

    optional<ext_congr_lemma> mk_ext_congr_lemma(expr const & e) const;

    entry const * get_entry(expr const & e) const { return m_state.m_entries.find(e); }
    bool check_invariant() const { return m_state.check_invariant(); }
};

struct ext_congr_lemma {
    /* The basic congr_lemma object defined at congr_lemma_manager */
    congr_lemma          m_congr_lemma;
    /* If m_heq_result is true, then lemma is based on heterogeneous equality
       and the conclusion is a heterogeneous equality. */
    unsigned             m_heq_result:1;
    /* If m_heq_lemma is true, then lemma was created using mk_hcongr_lemma. */
    unsigned             m_hcongr_lemma:1;
    ext_congr_lemma(congr_lemma const & H):
        m_congr_lemma(H), m_heq_result(false), m_hcongr_lemma(false) {}
};

void initialize_congruence_closure();
void finalize_congruence_closure();
}