#include "avltree.h"
#include "util.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static long compare_names(const void *key, void *value) {
        return strcmp(key, value);
}

static void test_basic(void) {
        AVLTree *tree;
        AVLTreeNode *node;
        const char *strings[] = { "ghi", "abc", "mno", "jkl", "def" };
        const char *sorted[] = { "abc", "def", "ghi", "jkl", "mno" };

        avl_tree_new(&tree, compare_names, freep);
        for (unsigned long i = 0; i < ARRAY_SIZE(strings); i += 1)
                avl_tree_insert(tree, strings[i], strdup(strings[i]));

        assert(avl_tree_get_n_elements(tree) == 5);

        assert(strcmp(avl_tree_find(tree, "abc"), "abc") == 0);
        assert(strcmp(avl_tree_find(tree, "jkl"), "jkl") == 0);
        assert(strcmp(avl_tree_find(tree, "mno"), "mno") == 0);
        assert(avl_tree_find(tree, "foo") == NULL);
        assert(avl_tree_remove(tree, "foo") == -AVL_ERROR_UNKNOWN_KEY);

        assert((node = avl_tree_first(tree)));
        for (unsigned long i = 0; i < ARRAY_SIZE(strings); i += 1) {
                assert(strcmp(avl_tree_node_get(node), sorted[i]) == 0);
                node = avl_tree_node_next(node);
        }
        assert(node == NULL);

        assert((node = avl_tree_last(tree)));
        for (long i = ARRAY_SIZE(strings) - 1; i >= 0; i -= 1) {
                assert(strcmp(avl_tree_node_get(node), sorted[i]) == 0);
                node = avl_tree_node_previous(node);
        }
        assert(node == NULL);

        for (long i = ARRAY_SIZE(strings) - 1; i >= 0; i -= 1)
                assert(avl_tree_remove(tree, strings[i]) == 0);

        assert(avl_tree_get_n_elements(tree) == 0);

        assert(avl_tree_free(tree) == NULL);
}

static void test_empty(void) {
        AVLTree *tree;

        avl_tree_new(&tree, compare_names, NULL);
        assert(avl_tree_find(tree, "foo") == NULL);
        assert(avl_tree_get_height(tree) == 0);
        assert(avl_tree_remove(tree, "foo") == -AVL_ERROR_UNKNOWN_KEY);
        assert(avl_tree_free(tree) == NULL);
}

static long compare_numbers(const void *key, void *node) {
        long a = (long)key;
        long b = (long)node;

        return a - b;
}

static void test_numbers(void) {
        AVLTree *tree;
        AVLTreeNode *node;
        long numbers[] = { 56, 23, 17, 41, 87, 72, 9, 31, 62, 99 };
        long sorted[] = { 9, 17, 23, 31, 41, 56, 62, 72, 87, 99 };

        avl_tree_new(&tree, compare_numbers, NULL);

        for (unsigned long i = 0; i < ARRAY_SIZE(numbers); i += 1)
                avl_tree_insert(tree, (void *)numbers[i], (void *)numbers[i]);

        assert(avl_tree_get_n_elements(tree) == ARRAY_SIZE(numbers));

        node = avl_tree_first(tree);
        for (unsigned long i = 0; i < ARRAY_SIZE(numbers); i += 1) {
                assert(node);
                assert((long)avl_tree_node_get(node) == sorted[i]);
                node = avl_tree_node_next(node);
        }
        assert(!node);

        assert(avl_tree_free(tree) == NULL);
}

static void test_worst_case(void) {
        AVLTree *tree;
        const unsigned long count = 10000;

        avl_tree_new(&tree, compare_numbers, NULL);

        for (unsigned long i = 0; i < count; i += 1)
                avl_tree_insert(tree, (void *)i, (void *)i);

        assert(avl_tree_get_n_elements(tree) == count);
        assert(avl_tree_get_height(tree) == (unsigned long)log2(count) + 1);

        for (unsigned long i = 0; i < count; i += 1)
                avl_tree_remove(tree, (void *)i);

        assert(avl_tree_get_n_elements(tree) == 0);
        assert(avl_tree_free(tree) == NULL);

        avl_tree_new(&tree, compare_numbers, NULL);

        for (unsigned long i = count; i > 0; i -= 1)
                avl_tree_insert(tree, (void *)i, (void *)i);

        assert(avl_tree_get_n_elements(tree) == count);
        assert(avl_tree_get_height(tree) == (unsigned long)log2(count) + 1);

        assert(avl_tree_free(tree) == NULL);
}

int main(void) {
        test_empty();
        test_basic();
        test_numbers();
        test_worst_case();

        return 0;
}
