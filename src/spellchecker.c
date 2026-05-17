/*
 * spellchecker.c
 * Trie (exact lookup) + BK-Tree (fuzzy suggestions) + Hash Table (correction cache)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_WORD_LEN    100
#define HASH_SIZE       1000
#define ALPHABET_SIZE   28
#define MAX_EDIT_DIST   30
#define MAX_SUGGESTIONS 10


/* ---- TRIE ---- */

struct TrieNode {
    struct TrieNode* children[ALPHABET_SIZE];
    int isEndOfWord;
};

struct TrieNode* createTrieNode() {
    struct TrieNode* node = (struct TrieNode*)malloc(sizeof(struct TrieNode));
    node->isEndOfWord = 0;
    for (int i = 0; i < ALPHABET_SIZE; i++)
        node->children[i] = NULL;
    return node;
}

int getIndex(char ch) {
    if (ch >= 'a' && ch <= 'z') return ch - 'a';
    if (ch == '\'') return 26;
    if (ch == '-')  return 27;
    return -1;
}

void insertTrie(struct TrieNode* root, const char* word) {
    struct TrieNode* cur = root;
    for (int i = 0; word[i]; i++) {
        int idx = getIndex(tolower(word[i]));
        if (idx == -1) continue;
        if (!cur->children[idx])
            cur->children[idx] = createTrieNode();
        cur = cur->children[idx];
    }
    cur->isEndOfWord = 1;
}

int searchTrie(struct TrieNode* root, const char* word) {
    struct TrieNode* cur = root;
    for (int i = 0; word[i]; i++) {
        int idx = getIndex(tolower(word[i]));
        if (idx == -1) continue;
        if (!cur->children[idx]) return 0;
        cur = cur->children[idx];
    }
    return cur->isEndOfWord;
}


/* ---- HASH TABLE (correction cache) ---- */

struct CacheNode {
    char word[MAX_WORD_LEN];
    char suggestion[MAX_WORD_LEN];
    struct CacheNode* next;
};

struct CacheNode* hashTable[HASH_SIZE];

unsigned int hash(const char* word) {
    unsigned int h = 0;
    while (*word) h = (h * 31 + *word++) % HASH_SIZE;
    return h;
}

void insertCache(const char* word, const char* suggestion) {
    unsigned int idx = hash(word);
    struct CacheNode* node = (struct CacheNode*)malloc(sizeof(struct CacheNode));
    strcpy(node->word, word);
    strcpy(node->suggestion, suggestion);
    node->next = hashTable[idx];
    hashTable[idx] = node;
}

char* searchCache(const char* word) {
    struct CacheNode* temp = hashTable[hash(word)];
    while (temp) {
        if (strcmp(temp->word, word) == 0) return temp->suggestion;
        temp = temp->next;
    }
    return NULL;
}


/* ---- EDIT DISTANCE (Damerau-Levenshtein) ----
 * Counts insertions, deletions, substitutions, and adjacent transpositions.
 * Transpositions ("teh" -> "the") cost 1 instead of 2, catching common typos.
 */

int min3(int a, int b, int c) {
    if (a < b && a < c) return a;
    return (b < c) ? b : c;
}

int editDistance(const char* a, const char* b) {
    int lenA = strlen(a), lenB = strlen(b);
    static int dp[MAX_WORD_LEN + 1][MAX_WORD_LEN + 1];

    for (int i = 0; i <= lenA; i++) dp[i][0] = i;
    for (int j = 0; j <= lenB; j++) dp[0][j] = j;

    for (int i = 1; i <= lenA; i++) {
        for (int j = 1; j <= lenB; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            dp[i][j] = 1 + min3(dp[i-1][j], dp[i][j-1], dp[i-1][j-1] + cost - 1);

            if (i > 1 && j > 1 && a[i-1] == b[j-2] && a[i-2] == b[j-1])
                dp[i][j] = (dp[i][j] < dp[i-2][j-2] + cost)
                            ? dp[i][j]
                            : dp[i-2][j-2] + cost;
        }
    }
    return dp[lenA][lenB];
}


/* ---- BK-TREE ---- */

struct BKNode {
    char word[MAX_WORD_LEN];
    struct BKNode* children[MAX_EDIT_DIST];
};

struct BKNode* createBKNode(const char* word) {
    struct BKNode* node = (struct BKNode*)malloc(sizeof(struct BKNode));
    strcpy(node->word, word);
    for (int i = 0; i < MAX_EDIT_DIST; i++)
        node->children[i] = NULL;
    return node;
}

void insertBK(struct BKNode* root, const char* word) {
    int dist = editDistance(word, root->word);
    if (dist == 0 || dist >= MAX_EDIT_DIST) return;
    if (!root->children[dist])
        root->children[dist] = createBKNode(word);
    else
        insertBK(root->children[dist], word);
}

typedef struct {
    char word[MAX_WORD_LEN];
    int  dist;
} Suggestion;

Suggestion suggestions[MAX_SUGGESTIONS];
int suggestionCount = 0;

void searchBK(struct BKNode* root, const char* word, int maxDist) {
    if (!root || suggestionCount >= MAX_SUGGESTIONS) return;

    int dist = editDistance(word, root->word);
    if (dist <= maxDist) {
        strncpy(suggestions[suggestionCount].word, root->word, MAX_WORD_LEN - 1);
        suggestions[suggestionCount].word[MAX_WORD_LEN - 1] = '\0';
        suggestions[suggestionCount].dist = dist;
        suggestionCount++;
    }

    int start = (dist - maxDist < 1) ? 1 : dist - maxDist;
    int end   = (dist + maxDist >= MAX_EDIT_DIST) ? MAX_EDIT_DIST - 1 : dist + maxDist;

    for (int i = start; i <= end; i++)
        if (root->children[i])
            searchBK(root->children[i], word, maxDist);
}

int cmpSuggestion(const void* a, const void* b) {
    return ((Suggestion*)a)->dist - ((Suggestion*)b)->dist;
}


/* ---- HELPERS ---- */

void toLowerStr(const char* src, char* dst) {
    int i;
    for (i = 0; src[i]; i++) dst[i] = tolower(src[i]);
    dst[i] = '\0';
}

/* Short words need strict matching; longer words tolerate more edits. */
int getSearchRadius(const char* word) {
    int len = strlen(word);
    if (len <= 4) return 1;
    if (len <= 8) return 2;
    return 3;
}


/* ---- MAIN ---- */

int main() {
    struct TrieNode* trieRoot = createTrieNode();
    struct BKNode*   bkRoot   = NULL;

    FILE* file = fopen("words.txt", "r");
    if (!file) {
        fprintf(stderr, "Error: 'words.txt' not found.\n");
        return 1;
    }

    char word[MAX_WORD_LEN];
    int  wordCount = 0;

    while (fscanf(file, "%99s", word) != EOF) {
        insertTrie(trieRoot, word);
        if (!bkRoot)
            bkRoot = createBKNode(word);
        else
            insertBK(bkRoot, word);
        wordCount++;
    }
    fclose(file);
    printf("Dictionary loaded: %d words.\n", wordCount);

    char input[MAX_WORD_LEN];
    char inputLower[MAX_WORD_LEN];

    while (1) {
        printf("\nEnter a word (or 'exit'): ");
        if (scanf("%99s", input) != 1) break;
        if (strcmp(input, "exit") == 0) break;

        toLowerStr(input, inputLower);

        char* cached = searchCache(inputLower);
        if (cached) {
            printf("'%s' not found. Cached correction: %s\n", inputLower, cached);
            continue;
        }

        if (searchTrie(trieRoot, inputLower)) {
            printf("'%s' is spelled correctly.\n", inputLower);
            continue;
        }

        printf("'%s' not found.\n", inputLower);

        suggestionCount = 0;
        int radius = getSearchRadius(inputLower);
        searchBK(bkRoot, inputLower, radius);

        if (suggestionCount == 0) {
            printf("No suggestions found.\n");
            continue;
        }

        qsort(suggestions, suggestionCount, sizeof(Suggestion), cmpSuggestion);

        for (int i = 0; i < suggestionCount; i++) {
            char response[10];
            printf("Did you mean '%s'? (edit distance: %d) (y/n): ",
                   suggestions[i].word, suggestions[i].dist);
            scanf("%9s", response);

            if (tolower(response[0]) == 'y') {
                insertCache(inputLower, suggestions[i].word);
                printf("Correction cached.\n");
                break;
            }

            if (i == suggestionCount - 1)
                printf("No suggestion accepted.\n");
        }
    }

    return 0;
}