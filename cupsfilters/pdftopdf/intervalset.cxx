#include "intervalset-private.h"
#include <stdio.h>
#include <assert.h>
#include <limits>
#include <algorithm>

const _cfPDFToPDFIntervalSet::key_t _cfPDFToPDFIntervalSet::npos=std::numeric_limits<_cfPDFToPDFIntervalSet::key_t>::max();

void _cfPDFToPDFIntervalSet::clear() // {{{
{
  data.clear();
}
// }}}

void _cfPDFToPDFIntervalSet::add(key_t start,key_t end) // {{{
{
  if (start<end) {
    data.push_back(std::make_pair(start,end));
  }
}
// }}}

void _cfPDFToPDFIntervalSet::finish() // {{{
{
  data_t::iterator it=data.begin(),end=data.end(),pos=it;
  if (it==end) {
    return;
  }

  std::sort(it,end);

  while (1) {
    ++it;
    if (it==end) {
      ++pos;
      break;
    }
    if (pos->second>=it->first) {
      pos->second=it->second;
    } else {
      ++pos;
      if (pos!=it) {
        *pos=*it;
      }
    }
  }

  data.erase(pos,data.end());
}
// }}}

bool _cfPDFToPDFIntervalSet::contains(key_t val) const // {{{
{
  data_t::const_iterator it=std::upper_bound(data.begin(),data.end(),std::make_pair(val,npos));
  if (it==data.begin()) {
    return false;
  }
  --it;
  return (val<it->second);
}
// }}}

_cfPDFToPDFIntervalSet::key_t _cfPDFToPDFIntervalSet::next(key_t val) const // {{{
{
  val++;
  data_t::const_iterator it=std::upper_bound(data.begin(),data.end(),std::make_pair(val,npos));
  if (it==data.begin()) {
    if (it==data.end()) { // empty
      return npos;
    }
    return it->first;
  }
  --it;
  if (val<it->second) {
    return val;
  }
  ++it;
  if (it==data.end()) {
    return npos;
  }
  return it->first;
}
// }}}

bool _cfPDFToPDFIntervalSet::intersect(const value_t &a,const value_t &b) const // {{{
{
  return ((a.first>=b.first) && (a.first<b.second)) ||
    ((b.first>=a.first) && (b.first<a.second));
}
// }}}

void _cfPDFToPDFIntervalSet::unite(value_t &aret,const value_t &b) const // {{{
{
  assert(intersect(aret,b));
  if (b.first<aret.first) {
    aret.first=b.first;
  }
  if (b.second>aret.second) {
    aret.second=b.second;
  }
}
// }}}

void _cfPDFToPDFIntervalSet::dump(pdftopdf_doc_t *doc) const // {{{
{
  int len=data.size();
  if (len==0) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: (empty)");
    return;
  }
  len--;
  for (int iA=0;iA<len;iA++) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: [%d,%d)",
				   data[iA].first, data[iA].second);
  }
  if (data[len].second==npos) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: [%d,inf)",
				   data[len].first);
  } else {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: [%d,%d)",
				   data[len].first, data[len].second);
  }
}
// }}}
