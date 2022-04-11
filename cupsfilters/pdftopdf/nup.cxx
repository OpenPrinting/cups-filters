#include "nup-private.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <utility>

void _cfPDFToPDFNupParameters::dump(pdftopdf_doc_t *doc) const // {{{
{
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: NupX: %d, NupY: %d, "
				 "width: %f, height: %f",
				 nupX,nupY,
				 width,height);

  int opos=-1,fpos=-1,spos=-1;
  if (xstart==pdftopdf_position_e::LEFT) { // or Bottom
    fpos=0;
  } else if (xstart==pdftopdf_position_e::RIGHT) { // or Top
    fpos=1;
  }
  if (ystart==pdftopdf_position_e::LEFT) { // or Bottom
    spos=0;
  } else if (ystart==pdftopdf_position_e::RIGHT) { // or Top
    spos=1;
  }
  if (first==pdftopdf_axis_e::X) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: First Axis: X");
    opos=0;
  } else if (first==pdftopdf_axis_e::Y) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: First Axis: Y");
    opos=2;
    std::swap(fpos,spos);
  }

  if ( (opos==-1)||(fpos==-1)||(spos==-1) ) {
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: Bad Spec: %d; start: %d, %d",
				   first,xstart,ystart);
  } else {
    static const char *order[4]={"lr","rl","bt","tb"};
    if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				   "cfFilterPDFToPDF: Order: %s%s",
				   order[opos+fpos],order[(opos+2)%4+spos]);
  }

  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: Alignment:");
  _cfPDFToPDFPositionDump(xalign,pdftopdf_axis_e::X,doc);
  _cfPDFToPDFPositionDump(yalign,pdftopdf_axis_e::Y,doc);
}
// }}}

bool _cfPDFToPDFNupParameters::possible(int nup) // {{{
{
  // 1 2 3 4 6 8 9 10 12 15 16
  return (nup>=1)&&(nup<=16)&&
         ( (nup!=5)&&(nup!=7)&&(nup!=11)&&(nup!=13)&&(nup!=14) );
}
// }}}

void _cfPDFToPDFNupParameters::preset(int nup,_cfPDFToPDFNupParameters &ret) // {{{
{
  switch (nup) {
  case 1:
    ret.nupX=1;
    ret.nupY=1;
    break;
  case 2:
    ret.nupX=2;
    ret.nupY=1;
    ret.landscape=true;
    break;
  case 3:
    ret.nupX=3;
    ret.nupY=1;
    ret.landscape=true;
    break;
  case 4:
    ret.nupX=2;
    ret.nupY=2;
    break;
  case 6:
    ret.nupX=3;
    ret.nupY=2;
    ret.landscape=true;
    break;
  case 8:
    ret.nupX=4;
    ret.nupY=2;
    ret.landscape=true;
    break;
  case 9:
    ret.nupX=3;
    ret.nupY=3;
    break;
  case 10:
    ret.nupX=5;
    ret.nupY=2;
    ret.landscape=true;
    break;
  case 12:
    ret.nupX=3;
    ret.nupY=4;
    break;
  case 15:
    ret.nupX=5;
    ret.nupY=3;
    ret.landscape=true;
    break;
  case 16:
    ret.nupX=4;
    ret.nupY=4;
    break;
  }
}
// }}}


_cfPDFToPDFNupState::_cfPDFToPDFNupState(const _cfPDFToPDFNupParameters &param) // {{{
  : param(param),
    in_pages(0),out_pages(0),
    nup(param.nupX*param.nupY),
    subpage(nup)
{
  assert( (param.nupX>0)&&(param.nupY>0) );
}
// }}}

void _cfPDFToPDFNupState::reset() // {{{
{
  in_pages=0;
  out_pages=0;
//  nup=param.nupX*param.nupY;
  subpage=nup;
}
// }}}

void _cfPDFToPDFNupPageEdit::dump(pdftopdf_doc_t *doc) const // {{{
{
  if (doc->logfunc) doc->logfunc(doc->logdata, CF_LOGLEVEL_DEBUG,
				 "cfFilterPDFToPDF: xpos: %f, ypos: %f, scale: %f",
				 xpos,ypos,scale);
  sub.dump(doc);
}
// }}}

std::pair<int,int> _cfPDFToPDFNupState::convert_order(int subpage) const // {{{
{
  int subx,suby;
  if (param.first==pdftopdf_axis_e::X) {
    subx=subpage%param.nupX;
    suby=subpage/param.nupX;
  } else {
    subx=subpage/param.nupY;
    suby=subpage%param.nupY;
  }

  subx=(param.nupX-1)*(param.xstart+1)/2-param.xstart*subx;
  suby=(param.nupY-1)*(param.ystart+1)/2-param.ystart*suby;

  return std::make_pair(subx,suby);
}
// }}}

static inline float lin(pdftopdf_position_e pos,float size) // {{{
{
  if (pos==-1) return 0;
  else if (pos==0) return size/2;
  else if (pos==1) return size;
  return size*(pos+1)/2;
}
// }}}

void _cfPDFToPDFNupState::calculate_edit(int subx,int suby,_cfPDFToPDFNupPageEdit &ret) const // {{{
{
  // dimensions of a "nup cell"
  const float width=param.width/param.nupX,
              height=param.height/param.nupY;

  // first calculate only for bottom-left corner
  ret.xpos=subx*width;
  ret.ypos=suby*height;

  const float scalex=width/ret.sub.width,
              scaley=height/ret.sub.height;
  float subwidth=ret.sub.width*scaley,
        subheight=ret.sub.height*scalex;

  // TODO?  if ( (!fitPlot)&&(ret.scale>1) ) ret.scale=1.0;
  if (scalex>scaley) {
    ret.scale=scaley;
    subheight=height;
    ret.xpos+=lin(param.xalign,width-subwidth);
  } else {
    ret.scale=scalex;
    subwidth=width;
    ret.ypos+=lin(param.yalign,height-subheight);
  }

  ret.sub.left=ret.xpos;
  ret.sub.bottom=ret.ypos;
  ret.sub.right=ret.sub.left+subwidth;
  ret.sub.top=ret.sub.bottom+subheight;
}
// }}}

bool _cfPDFToPDFNupState::mext_page(float in_width,float in_height,_cfPDFToPDFNupPageEdit &ret) // {{{
{
  in_pages++;
  subpage++;
  if (subpage>=nup) {
    subpage=0;
    out_pages++;
  }

  ret.sub.width=in_width;
  ret.sub.height=in_height;

  auto sub=convert_order(subpage);
  calculate_edit(sub.first,sub.second,ret);

  return (subpage==0);
}
// }}}


static std::pair<pdftopdf_axis_e,pdftopdf_position_e> parsePosition(char a,char b) // {{{ returns ,CENTER(0) on invalid
{
  a|=0x20; // make lowercase
  b|=0x20;
  if ( (a=='l')&&(b=='r') ) {
    return std::make_pair(pdftopdf_axis_e::X,pdftopdf_position_e::LEFT);
  } else if ( (a=='r')&&(b=='l') ) {
    return std::make_pair(pdftopdf_axis_e::X,pdftopdf_position_e::RIGHT);
  } else if ( (a=='t')&&(b=='b') ) {
    return std::make_pair(pdftopdf_axis_e::Y,pdftopdf_position_e::TOP);
  } else if ( (a=='b')&&(b=='t') ) {
    return std::make_pair(pdftopdf_axis_e::Y,pdftopdf_position_e::BOTTOM);
  } 
  return std::make_pair(pdftopdf_axis_e::X,pdftopdf_position_e::CENTER);
}
// }}}

bool _cfPDFToPDFParseNupLayout(const char *val,_cfPDFToPDFNupParameters &ret) // {{{
{
  assert(val);
  auto pos0=parsePosition(val[0],val[1]);
  if (pos0.second==CENTER) {
    return false;
  }
  auto pos1=parsePosition(val[2],val[3]);
  if ( (pos1.second==CENTER)||(pos0.first==pos1.first) ) {
    return false;
  }

  ret.first=pos0.first;
  if (ret.first==pdftopdf_axis_e::X) {
    ret.xstart=pos0.second;
    ret.ystart=pos1.second;
  } else {
    ret.xstart=pos1.second;
    ret.ystart=pos0.second;
  }

  return (val[4]==0); // everything seen?
}
// }}}

