#include "avltree.h"
#include "util.h"

struct AVLTree {
        AVLTreeNode *root;
        AVLCompareFunc compare;
        AVLFreepFunc freep;
        unsigned long n_elements;
};

struct AVLTreeNode {
        void *value;
        AVLTreeNode *parent, *left, *right;
        long height;
};

AVLTreeNode *avl_tree_node_next(AVLTreeNode *node) {
        AVLTreeNode *next = NULL;

        if (node->right) {
                next = node->right;

                while (next->left)
                        next = next->left;

        } else {
                while ((next = node->parent) && next->right == node)
                        node = next;
        }

        return next;
}

AVLTreeNode *avl_tree_node_previous(AVLTreeNode *node) {
        AVLTreeNode *previous = NULL;

        if (node->left) {
                previous = node->left;

                while (previous->right)
                        previous = previous->right;

        } else {
                while ((previous = node->parent) && previous->left == node)
                        node = previous;
        }

        return previous;
}

void *avl_tree_node_get(AVLTreeNode *node) {
        return node->value;
}

/*
 *          a                  b
 *         / \                / \
 *        t1  b     --->     a  t3
 *           / \            / \
 *          t2 t3          t1 t2
 */
static AVLTreeNode *node_rotate_left(AVLTreeNode *a) {
        AVLTreeNode *b = a->right;
        AVLTreeNode *t1 = a->left;
        AVLTreeNode *t2 = b->left;
        AVLTreeNode *t3 = b->right;
        AVLTreeNode *p = a->parent;

        if (p) {
                if (p->left == a)
                        p->left = b;
                else
                        p->right = b;
        }

        a->parent = b;
        a->left = t1;
        a->right = t2;
        a->height = 1 + MAX(t1 ? t1->height : 0, t2 ? t2->height : 0);

        b->parent = p;
        b->left = a;
        b->right = t3;
        b->height = 1 + MAX(a->height, t3 ? t3->height : 0);

        if (t2)
                t2->parent = a;

        return b;
}

/*
 *         a                b
 *        / \              / \
 *       b  t3    --->    t1  a
 *      / \                  / \
 *     t1 t2                t2 t3
 */
static AVLTreeNode *node_rotate_right(AVLTreeNode *a) {
        AVLTreeNode *b = a->left;
        AVLTreeNode *t1 = b->left;
        AVLTreeNode *t2 = b->right;
        AVLTreeNode *t3 = a->right;
        AVLTreeNode *p = a->parent;

        if (p) {
                if (p->left == a)
                        p->left = b;
                else
                        p->right = b;
        }

        a->parent = b;
        a->left = t2;
        a->right = t3;
        a->height = 1 + MAX(t2 ? t2->height : 0, t3 ? t3->height : 0);

        b->parent = p;
        b->left = t1;
        b->right = a;
        b->height = 1 + MAX(t1 ? t1->height : 0, a->height);

        if (t2)
                t2->parent = a;

        return b;
}

static long node_get_balance(AVLTreeNode *node) {
        return (node->right ? node->right->height : 0) - (node->left ? node->left->height : 0);
}

static AVLTreeNode *node_rebalance(AVLTreeNode *node) {
        int balance;

        balance = node_get_balance(node);
        if (balance < -1) {
                if (node_get_balance(node->left) <= 0) {
                        node = node_rotate_right(node);
                } else {
                        node->left = node_rotate_left(node->left);
                        node = node_rotate_right(node);
                }
        } else if (balance > 1) {
                if (node_get_balance(node->right) >= 0) {
                        node = node_rotate_left(node);
                } else {
                        node->right = node_rotate_right(node->right);
                        node = node_rotate_left(node);
                }
        }

        return node;
}

long avl_tree_new(AVLTree **treep, AVLCompareFunc compare, AVLFreepFunc fp) {
        AVLTree *tree;

        tree = calloc(1, sizeof(AVLTree));
        if (!tree)
                return -AVL_ERROR_PANIC;

        tree->compare = compare;
        tree->freep = fp;

        *treep = tree;
        return 0;
}

static void avl_tree_free_subtree(AVLTree *tree, AVLTreeNode *node) {
        if (!node)
                return;

        avl_tree_free_subtree(tree, node->left);
        avl_tree_free_subtree(tree, node->right);

        if (tree->freep)
                tree->freep(&node->value);

        free(node);
}

AVLTree *avl_tree_free(AVLTree *tree) {
        avl_tree_free_subtree(tree, tree->root);
        free(tree);

        return NULL;
}

AVLTreeNode *avl_tree_first(AVLTree *tree) {
        AVLTreeNode *node = tree->root;

        if (!node)
                return NULL;

        while (node->left)
                node = node->left;

        return node;
}

AVLTreeNode *avl_tree_last(AVLTree *tree) {
        AVLTreeNode *node = tree->root;

        if (!node)
                return NULL;

        while (node->right)
                node = node->right;

        return node;
}

unsigned long avl_tree_get_n_elements(AVLTree *tree) {
        return tree->n_elements;
}

long avl_tree_get_elements(AVLTree *tree, void ***elementsp) {
        void **elements = NULL;
        AVLTreeNode *node;
        unsigned long i = 0;

        elements = malloc((tree->n_elements + 1) * sizeof(void *));
        if (!elements)
                return -AVL_ERROR_PANIC;

        node = avl_tree_first(tree);
        while (node) {
                elements[i] = avl_tree_node_get(node);
                node = avl_tree_node_next(node);
                i += 1;
        }

        elements[tree->n_elements] = NULL;

        *elementsp = elements;

        return tree->n_elements;
}

static long avl_tree_insert_subtree(AVLTree *tree,
                                    AVLTreeNode **nodep,
                                    const void *key,
                                    void *value) {
        AVLTreeNode *node;
        long d;
        long r;

        node = *nodep;

        if (!node) {
                node = calloc(1, sizeof(AVLTreeNode));
                if (!node)
                        return -AVL_ERROR_PANIC;

                node->value = value;
                node->height = 1;

                *nodep = node;
                return 0;
        }

        d = tree->compare(key, node->value);
        if (d == 0)
                return -AVL_ERROR_KEY_EXISTS;

        if (d < 0) {
                r = avl_tree_insert_subtree(tree, &node->left, key, value);
                if (r < 0)
                        return r;

                node->left->parent = node;
        } else {
                r = avl_tree_insert_subtree(tree, &node->right, key, value);
                if (r < 0)
                        return r;

                node->right->parent = node;
        }

        node->height = 1 + MAX(node->left ? node->left->height : 0,
                               node->right ? node->right->height : 0);

        *nodep = node_rebalance(node);

        return 0;
}

long avl_tree_insert(AVLTree *tree, const void *key, void *value) {
        long r;

        r = avl_tree_insert_subtree(tree, &tree->root, key, value);
        if (r < 0)
                return r;

        tree->n_elements += 1;
        return 0;
}

static AVLTreeNode *rebalance_from(AVLTreeNode *node) {
        for (;;) {
                node->height = 1 + MAX(node->left ? node->left->height : 0,
                                       node->right ? node->right->height : 0);
                node = node_rebalance(node);

                if (!node->parent)
                        return node;

                node = node->parent;
        }
}

long avl_tree_remove(AVLTree *tree, const void *key) {
        AVLTreeNode *node;
        AVLTreeNode *changed = NULL;

        node = avl_tree_find_node(tree, key);
        if (!node)
                return -AVL_ERROR_UNKNOWN_KEY;

        if (tree->freep)
                tree->freep(&node->value);

        if (node->left) {
                AVLTreeNode *rightmost = node->left;

                while (rightmost->right)
                        rightmost = rightmost->right;

                node->value = rightmost->value;

                if (rightmost->left) {
                        rightmost->value = rightmost->left->value;
                        free(rightmost->left);
                        rightmost->left = NULL;
                        changed = rightmost;
                } else {
                        if (rightmost == rightmost->parent->left)
                                rightmost->parent->left = NULL;
                        else
                                rightmost->parent->right = NULL;
                        changed = rightmost->parent;
                        free(rightmost);
                }

        } else if (node->right) {
                /*
                 * If the left subtree is empty, the right subtree can
                 * only contain a single node, because of the height
                 * invariant.
                 */
                node->value = node->right->value;
                free(node->right);
                node->right = NULL;

                changed = node;

        } else {
                if (node->parent) {
                        if (node == node->parent->right)
                                node->parent->right = NULL;
                        else
                                node->parent->left = NULL;

                        changed = node->parent;
                } else {
                        tree->root = NULL;
                }

                free(node);
        }

        if (changed)
                tree->root = rebalance_from(changed);

        tree->n_elements -= 1;

        return 0;
}

void *avl_tree_find(AVLTree *tree, const void *key) {
        AVLTreeNode *node;

        node = avl_tree_find_node(tree, key);

        return node ? node->value : NULL;
}

AVLTreeNode *avl_tree_find_node(AVLTree *tree, const void *key) {
        AVLTreeNode *node = tree->root;

        while (node) {
                long r = tree->compare(key, node->value);

                if (r == 0)
                        break;
                else if (r < 0)
                        node = node->left;
                else
                        node = node->right;
        }

        return node;
}

unsigned long avl_tree_get_height(AVLTree *tree) {
        return tree->root ? tree->root->height : 0;
}

long avl_tree_ptr_compare(const void *key, void *value) {
        unsigned long a = (unsigned long)key;
        unsigned long b = (unsigned long)value;

        if (a < b)
                return -1;
        else if (a == b)
                return 0;
        else
                return 1;
}
