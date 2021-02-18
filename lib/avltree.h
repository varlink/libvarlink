// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
        AVL_ERROR_PANIC = 1,
        AVL_ERROR_KEY_EXISTS,
        AVL_ERROR_UNKNOWN_KEY
};

typedef struct AVLTree AVLTree;
typedef struct AVLTreeNode AVLTreeNode;

typedef long (*AVLCompareFunc)(const void *key, void *value);

/* ThIs is the same signature as a cleanup function, a freep() not a plain free() */
typedef void (*AVLFreepFunc)(void *value);

/*
 * Creates a new AVLTree which sorts its elements with @compare and
 * frees them with free().
 *
 * Pass avl_tree_ptr_compare() as @compare if keys will be pointer
 * values.
 */
long avl_tree_new(AVLTree **treep, AVLCompareFunc compare, AVLFreepFunc fp);

/*
 * Frees @tree and calls the the free function passed to avl_tree_new()
 * on every element.
 */
AVLTree *avl_tree_free(AVLTree *tree);

unsigned long avl_tree_get_n_elements(AVLTree *tree);

/*
 * Collects all elements in @tree into a newly-allocated array at
 * @elementsp, which will be terminated with a NULL element.
 *
 * Returns the number of elements in @tree.
 */
long avl_tree_get_elements(AVLTree *tree, void ***elementsp);

/*
 * Returns the smallest node in @tree (as defined by the compare
 * function), or NULL if the tree is empty.
 */
AVLTreeNode *avl_tree_first(AVLTree *tree);

/*
 * Returns the largest node in @tree (as defined by the compare
 * function), or NULL if the tree is empty.
 */
AVLTreeNode *avl_tree_last(AVLTree *tree);

/*
 * Returns the next-largest node after @node (as defined by the compare
 * function.
 */
AVLTreeNode *avl_tree_node_next(AVLTreeNode *node);

/*
 * Returns the next-smallest node after @node (as defined by the compare
 * function.
 */
AVLTreeNode *avl_tree_node_previous(AVLTreeNode *node);

/*
 * Returns the value that @node holds.
 */
void *avl_tree_node_get(AVLTreeNode *node);

/*
 * Returns the value in @tree for which compare(key, value) returns 0, or
 * NULL if no such value exists.
 */
void *avl_tree_find(AVLTree *tree, const void *key);

/*
 * Returns the node in @tree for which compare(key, node->value) returns
 * 0, or NULL if no such value exists.
 */
AVLTreeNode *avl_tree_find_node(AVLTree *tree, const void *key);

/*
 * Inserts @value into @tree, using @key to find its position.
 *
 * Returns -EEXIST if a value was already inserted with @key.
 */
long avl_tree_insert(AVLTree *tree, const void *key, void *value);

/*
 * Removes the node at @key from @tree.
 *
 * Returns -ENOENT if no such node exists.
 */
long avl_tree_remove(AVLTree *tree, const void *key);


/*
 * A compare function that can be passed to avl_tree_new() if keys are
 * pointers.
 */
long avl_tree_ptr_compare(const void *key, void *value);

/* private */
unsigned long avl_tree_get_height(AVLTree *tree);
