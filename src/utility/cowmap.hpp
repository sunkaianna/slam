//
//  cowmap.hpp
//  slam
//
//  Created by Roshan Shariff on 11-10-10.
//  Copyright 2011 University of Alberta. All rights reserved.
//

#ifndef slam_cowmap_hpp
#define slam_cowmap_hpp

#include <utility>
#include <cstddef>

#include <boost/compressed_pair.hpp>

#include "utility/utility.hpp"
#include "utility/cowtree.hpp"

template <class K, class V, class Compare = std::less<K>>
class cowmap {
    
public:
    
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<const K, V>;
    using reference = value_type&;
    using const_reference = const value_type&;
    using size_type = std::size_t;
    using key_compare = Compare;
    
    struct value_compare {
        explicit value_compare (const key_compare& cmp) : key_cmp(cmp) { }
        bool operator() (const value_type& lhs, const value_type& rhs) const {
            return key_cmp (lhs.first, rhs.first);
        }
        const key_compare& get_key_compare() const { return key_cmp; }
        key_compare& get_key_compare() { return key_cmp; }
    private:
        key_compare key_cmp;
    };
    
    cowmap (key_compare cmp = key_compare()) : data (cowtree::root(), value_compare(cmp)) { }
    
    const value_compare& value_comp () const { return data.second(); }
    value_compare& value_comp () { return data.second(); }
    
    const key_compare& key_comp () const { return value_comp().get_key_compare(); }
    key_compare& key_comp () { return value_comp().get_key_compare(); }
    
    bool empty () const { return root().empty(); }
    
    void clear () { return root().clear(); }
    
    mapped_type get (const key_type& key) const {
        return find_subtree(key).template value<value_type>().second;
    }
    
    template <class Functor>
    void for_each (Functor&& f) const { inorder_traverse (root(), std::forward<Functor>(f)); }
    
    bool insert (const value_type& entry) {
        cowtree::editor editor (root());
        return insert (entry, editor);
    }
    
    bool insert (const key_type& key, const mapped_type& value) {
        return insert (value_type (key, value));
    }

    void swap (cowmap& other) { data.swap (other.data); }
    
    size_type count (const key_type& key) const { return find_subtree(key).empty() ? 0 : 1; }
    
private:
    
    const cowtree::root& root () const { return data.first(); }
    cowtree::root& root () { return data.first(); }
    
    const cowtree& find_subtree (const key_type& key) const;
    
    bool insert (const value_type& entry, cowtree::editor& editor);
    
    template <class Functor> void inorder_traverse (const cowtree&, Functor&&) const;
    
    boost::compressed_pair<cowtree::root, value_compare> data;
    
};


template <class K, class V, class Compare>
const cowtree& cowmap<K, V, Compare>::find_subtree (const key_type& key) const {
    const cowtree* subtree = &root();
    while (!subtree->empty()) {
        if (key_comp()(key, subtree->value<value_type>().first)) subtree = &subtree->left();
        else if (key_comp()(subtree->value<value_type>().first, key)) subtree = &subtree->right();
        else break;
    }
    return *subtree;
}


template <class K, class V, class Compare>
bool cowmap<K, V, Compare>::insert (const value_type& entry, cowtree::editor& editor) {
    if (editor.subtree().empty()) {
        editor.insert<value_type>(entry);
        return true;
    }
    else {
        value_type& node_value = editor.template value<value_type>();
        if (value_comp()(entry, node_value)) {
            cowtree::editor::left left_editor (editor);
            return insert (entry, left_editor);
        }
        else if (value_comp()(node_value, entry)) {
            cowtree::editor::right right_editor (editor);
            return insert (entry, right_editor);
        }
        else {
            node_value.second = entry.second;
            return false;
        }
    }
}


template <class K, class V, class Compare>
template <class Functor>
void cowmap<K, V, Compare>::inorder_traverse (const cowtree& subtree, Functor&& f) const {
    if (!subtree.empty()) {
        inorder_traverse (subtree.left(), std::forward<Functor>(f));
        std::forward<Functor>(f) (subtree.value<value_type>().first,
                                  subtree.value<value_type>().second);
        inorder_traverse (subtree.right(), std::forward<Functor>(f));
    }
}

#endif
