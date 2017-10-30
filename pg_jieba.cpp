/*-------------------------------------------------------------------------
 *
 * pg_jieba.c
 *    a text search parser for Chinese
 *
 * Author: Jaimin Pan <jaimin.pan@gmail.com>
 *
 * IDENTIFICATION
 *    pg_jieba.cpp
 *
 *-------------------------------------------------------------------------
 */

#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

#include <libintl.h>
#include "postgres.h"
#include "utils/guc.h"
#include "utils/elog.h"
#include "miscadmin.h"
#include "tsearch/ts_public.h"

#ifdef __cplusplus
}
#endif

#include "cppjieba/Jieba.hpp"

namespace pg_jieba {

using namespace cppjieba;
using namespace std;

static const char* const tok_alias[] = {
  "",
  "nz",
  "n",
  "m",
  "i",
  "l",
  "d",
  "s",
  "t",
  "mq",
  "nr",
  "j",
  "a",
  "r",
  "b",
  "f",
  "nrt",
  "v",
  "z",
  "ns",
  "q",
  "vn",
  "c",
  "nt",
  "u",
  "o",
  "zg",
  "nrfg",
  "df",
  "p",
  "g",
  "y",
  "ad",
  "vg",
  "ng",
  "x",
  "ul",
  "k",
  "ag",
  "dg",
  "rr",
  "rg",
  "an",
  "vq",
  "e",
  "uv",
  "tg",
  "mg",
  "ud",
  "vi",
  "vd",
  "uj",
  "uz",
  "h",
  "ug",
  "rz"
};

static const char* const lex_descr[] = {
  "",
  "other proper noun",
  "noun",
  "numeral",
  "idiom",
  "temporary idiom",
  "adverb",
  "space",
  "time",
  "numeral-classifier compound",
  "person's name",
  "abbreviate",
  "adjective",
  "pronoun",
  "difference",
  "direction noun",
  "nrt",
  "verb",
  "z",
  "location",
  "quantity",
  "vn",
  "conjunction",
  "organization",
  "auxiliary",
  "onomatopoeia",
  "zg",
  "nrfg",
  "df",
  "prepositional",
  "morpheme",
  "modal verbs",
  "ad",
  "vg",
  "ng",
  "unknown",
  "ul",
  "k",
  "ag",
  "dg",
  "rr",
  "rg",
  "an",
  "vq",
  "exclamation",
  "uv",
  "tg",
  "mg",
  "ud",
  "vi",
  "vd",
  "uj",
  "uz",
  "h",
  "ug",
  "rz"
};

/*
 * types
 */
typedef struct
{
  vector<string>::iterator iter;
  vector<string>* words;
} JiebaCtx;

#ifdef __cplusplus
extern "C" {
#endif
PG_MODULE_MAGIC;

void    _PG_init(void);
void    _PG_fini(void);

/*
 * prototypes
 */
PG_FUNCTION_INFO_V1(jieba_start);
Datum jieba_start(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jieba_query_start);
Datum jieba_query_start(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jieba_gettoken);
Datum jieba_gettoken(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jieba_end);
Datum jieba_end(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jieba_lextype);
Datum jieba_lextype(PG_FUNCTION_ARGS);
#ifdef __cplusplus
}
#endif

/* wrapper class for all we used */
class PgJieba {
public:
  PgJieba(const string& dict_path,
          const string& model_path,
          const string& user_dict_path)
    : dict_trie_(dict_path, user_dict_path),
      hmm_model_(model_path),
      mix_seg_(&dict_trie_, &hmm_model_),
      query_seg_(&dict_trie_, &hmm_model_) {
    auto num_types = sizeof(tok_alias) / sizeof(tok_alias[0]);
    for (auto i = 1; i < num_types; ++i) {
      lex_id_.insert({tok_alias[i], i});
    }
  }

  void Cut(const string& sentence, vector<string>& words, bool hmm = true) const {
    mix_seg_.Cut(sentence, words, hmm);
  }
  void Cut(const string& sentence, vector<Word>& words, bool hmm = true) const {
    mix_seg_.Cut(sentence, words, hmm);
  }

  void CutForSearch(const string& sentence, vector<string>& words, bool hmm = true) const {
    query_seg_.Cut(sentence, words, hmm);
  }
  void CutForSearch(const string& sentence, vector<Word>& words, bool hmm = true) const {
    query_seg_.Cut(sentence, words, hmm);
  }

  string LookupTag(const string &str) const {
    return mix_seg_.LookupTag(str);
  }

  int LookupLexTypeId(const string &str) const {
    try {
      return lex_id_.at(this->LookupTag(str));
    } catch(...) {
      return lex_id_.at("n");
    }
  }

  int GetLexTypeSize() const {
    return lex_id_.size();
  }

private:
  DictTrie dict_trie_;
  HMMModel hmm_model_;

  // They share the same dict trie and model
  MixSegment mix_seg_;
  QuerySegment query_seg_;
  unordered_map<string, int> lex_id_;
}; // class PgJieba

static PgJieba* jieba = nullptr;
static const char* DICT_PATH = "jieba.dict";
static const char* HMM_PATH = "jieba.hmm_model";
static const char* USER_DICT = "jieba.user.dict";
static const char* EXT = "utf8";

static char* jieba_get_tsearch_config_filename(const char *basename, const char *extension);

/*
 * Module load callback
 */
void _PG_init(void)
{
  if (jieba != nullptr) {
    return;
  }

  /*
   init will take a few seconds to load dicts.
   */
  jieba = new PgJieba(jieba_get_tsearch_config_filename(DICT_PATH, EXT),
                      jieba_get_tsearch_config_filename(HMM_PATH, EXT),
                      jieba_get_tsearch_config_filename(USER_DICT, EXT));
}

/*
 * Module unload callback
 */
void _PG_fini(void)
{
  if (jieba != nullptr) {
    delete jieba;
    jieba = nullptr;
  }
}

/*
 * functions
 */
Datum jieba_start(PG_FUNCTION_ARGS)
{
  string str(static_cast<char*>(PG_GETARG_POINTER(0)),
                 static_cast<unsigned long>(PG_GETARG_INT32(1)));
  auto words = new vector<string>();
  jieba->Cut(str, *words);

  JiebaCtx* const ctx = static_cast<JiebaCtx*>(palloc0(sizeof(JiebaCtx)));
  ctx->words = words;
  ctx->iter = words->begin();
  PG_RETURN_POINTER(ctx);
}

Datum jieba_query_start(PG_FUNCTION_ARGS)
{
  string str(static_cast<char*>(PG_GETARG_POINTER(0)),
             static_cast<unsigned long>(PG_GETARG_INT32(1)));
  auto words = new vector<string>();
  jieba->CutForSearch(str, *words);

  JiebaCtx* const ctx = static_cast<JiebaCtx*>(palloc0(sizeof(JiebaCtx)));
  ctx->words = words;
  ctx->iter = words->begin();
  PG_RETURN_POINTER(ctx);
}

Datum jieba_gettoken(PG_FUNCTION_ARGS)
{
  JiebaCtx* const ctx = reinterpret_cast<JiebaCtx*>(PG_GETARG_POINTER(0));
  char** t = reinterpret_cast<char**>(PG_GETARG_POINTER(1));
  int* tlen = reinterpret_cast<int*>(PG_GETARG_POINTER(2));
  int type = -1;

  auto& cur_iter = ctx->iter;

  /* already done the work, or no sentence */
  if (cur_iter == ctx->words->end()) {
    *tlen = 0;
    type = 0;

    PG_RETURN_INT32(type);
  }

  type = jieba->LookupLexTypeId(*cur_iter);
  *tlen = static_cast<int>(cur_iter->length());
  *t = const_cast<char*>(cur_iter->c_str());

  ++cur_iter;
  PG_RETURN_INT32(type);
}

Datum jieba_end(PG_FUNCTION_ARGS)
{
  JiebaCtx* const ctx = reinterpret_cast<JiebaCtx*>(PG_GETARG_POINTER(0));
  if (ctx->words != nullptr) {
    delete ctx->words;
    ctx->words = nullptr;
  }
  pfree(ctx);
  PG_RETURN_VOID();
}

Datum jieba_lextype(PG_FUNCTION_ARGS)
{
  auto size = jieba->GetLexTypeSize();
  LexDescr *descr = static_cast<LexDescr*>(palloc(sizeof(LexDescr) * size));

  for (int i = 1; i <= size; i++) {
    descr[i - 1].lexid = i;
    descr[i - 1].alias = pstrdup(tok_alias[i]);
    descr[i - 1].descr = pstrdup(lex_descr[i]);
  }

  descr[size].lexid = 0;

  PG_RETURN_POINTER(descr);
}


/*
 * Given the base name and extension of a tsearch config file, return
 * its full path name.  The base name is assumed to be user-supplied,
 * and is checked to prevent pathname attacks.  The extension is assumed
 * to be safe.
 *
 * The result is a palloc'd string.
 */
static char* jieba_get_tsearch_config_filename(const char *basename,
                                               const char *extension)
{
  char sharepath[MAXPGPATH];

  /*
   * We limit the basename to contain a-z, 0-9, and underscores.  This may
   * be overly restrictive, but we don't want to allow access to anything
   * outside the tsearch_data directory, so for instance '/' *must* be
   * rejected, and on some platforms '\' and ':' are risky as well. Allowing
   * uppercase might result in incompatible behavior between case-sensitive
   * and case-insensitive filesystems, and non-ASCII characters create other
   * interesting risks, so on the whole a tight policy seems best.
   */
  if (strspn(basename, "abcdefghijklmnopqrstuvwxyz0123456789_.") != strlen(basename))
    ereport(ERROR,
        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
         errmsg("invalid text search configuration file name \"%s\"",
            basename)));

  get_share_path(my_exec_path, sharepath);
  char* result = static_cast<char*>(palloc(MAXPGPATH));
  snprintf(result, MAXPGPATH, "%s/tsearch_data/%s.%s",
       sharepath, basename, extension);

  return result;
}

}; // namespace pg_jieba
