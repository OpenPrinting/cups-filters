#ifndef _CUPS_FILTERS_PDFTOPDF_INTERVALSET_H_
#define _CUPS_FILTERS_PDFTOPDF_INTERVALSET_H_

#include "pdftopdf-private.h"
#include <stddef.h>
#include <vector>

class _cfPDFToPDFIntervalSet {
  typedef int key_t; // TODO?! template <typename T>
  typedef std::pair<key_t, key_t> value_t;
  typedef std::vector<value_t> data_t;
 public:
  static const key_t npos;

  void clear();
  // [start; end) !
  void add(key_t start, key_t end = npos);
  void finish();

  size_t size() const { return data.size(); }

  // only after finish() has been called:
  bool contains(key_t val) const;
  key_t next(key_t val) const;

  void dump(pdftopdf_doc_t *doc) const;
 private:
  // currently not used
  bool intersect(const value_t &a, const value_t &b) const;
  void unite(value_t &aret, const value_t &b) const;
 private:
  data_t data;
};

#endif
