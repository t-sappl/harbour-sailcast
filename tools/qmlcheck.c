#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree_sitter/api.h"
const TSLanguage *tree_sitter_qmljs(void);
static int errors = 0;
static void walk(TSNode n, const char *file) {
    if (ts_node_is_error(n) || ts_node_is_missing(n)) {
        TSPoint p = ts_node_start_point(n);
        printf("%s:%u:%u: %s\n", file, p.row + 1, p.column + 1, ts_node_is_missing(n) ? "missing" : "syntax error");
        errors++;
        return;
    }
    if (!ts_node_has_error(n)) return;
    for (uint32_t i = 0; i < ts_node_child_count(n); i++) walk(ts_node_child(n, i), file);
}
int main(int argc, char **argv) {
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_qmljs());
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        char *buf = malloc(len + 1); fread(buf, 1, len, f); fclose(f);
        TSTree *tree = ts_parser_parse_string(parser, NULL, buf, len);
        walk(ts_tree_root_node(tree), argv[a]);
        ts_tree_delete(tree); free(buf);
    }
    printf("%d errors\n", errors);
    return errors != 0;
}
