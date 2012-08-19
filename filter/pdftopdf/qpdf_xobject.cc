#include "qpdf_xobject.h"
//#include <qpdf/Types.h>
#include <qpdf/QPDF.hh>
#include <qpdf/Pl_Discard.hh>
#include <qpdf/Pl_Count.hh>
#include <qpdf/Pl_Concatenate.hh>
#include "qpdf_tools.h"
#include "qpdf_pdftopdf.h"

// TODO: need to remove  Struct Parent stuff  (or fix)

// NOTE: use /TrimBox to position content inside Nup cell, /BleedBox to clip against

class CombineFromContents_Provider : public QPDFObjectHandle::StreamDataProvider {
public:
  CombineFromContents_Provider(const std::vector<QPDFObjectHandle> &contents);

  void provideStreamData(int objid, int generation, Pipeline* pipeline);
private:
  std::vector<QPDFObjectHandle> contents;
};

CombineFromContents_Provider::CombineFromContents_Provider(const std::vector<QPDFObjectHandle> &contents)
  : contents(contents)
{
}

void CombineFromContents_Provider::provideStreamData(int objid, int generation, Pipeline* pipeline)
{
  Pl_Concatenate concat("concat", pipeline);
  const int clen=contents.size();
  for (int iA=0;iA<clen;iA++) {
    contents[iA].pipeStreamData(&concat, true, false, false);
  }
  concat.manualFinish();
}

/*
To convert a page to an XObject there are several keys to consider:

/Type /Page        -> /Type /XObject (/Type optional for XObject)
                   -> /Subtype /Form
                   -> [/FormType 1]  (optional)
/Parent ? ? R      -> remove
/Resources dict    -> copy
/MediaBox rect [/CropBox /BleedBox /TrimBox /ArtBox] 
                   -> /BBox  (use TrimBox [+ Bleed consideration?], with fallback to /MediaBox)
                      note that /BBox is in *Form Space*, see /Matrix!
[/BoxColorInfo dict]   (used for guidelines that may be shown by viewer)
                   -> ignore/remove
[/Contents asfd]   -> concatenate into stream data of the XObject (page is a dict, XObject a stream)

[/Rotate 90]   ... must be handled (either use CTM where XObject is /used/ -- or set /Matrix)
[/UserUnit] (PDF 1.6)   -> to /Matrix ?   -- it MUST be handled.

[/Group dict]      -> copy
[/Thumb stream]    -> remove, not needed any more / would have to be regenerated (combined)
[/B]               article beads -- ignore for now
[/Dur]             -> remove  (transition duration)
[/Trans]           -> remove  (transitions)
[/AA]              -> remove  (additional-actions)

[/Metadata]        what shall we do?? (kill: we can't combine XML)
[/PieceInfo]       -> remove, we can't combine private app data (?)
[/LastModified  date]  (opt except /PieceInfo)  -> see there

[/PZ]              -> remove, can't combine/keep (preferred zoom level)
[/SeparationInfo]  -> remove, no way to keep this (needed for separation)

[/ID]              related to web capture -- ignore/kill?
[/StructParents]   (opt except pdf contains "structural content items")
                   -> copy (is this correct?)

[/Annots]          annotations -- ignore for now
[/Tabs]            tab order for annotations (/R row, /C column, /S structure order) -- see /Annots

[/TemplateInstantiated]  (reqd, if page was created from named page obj, 1.5) -- ? just ignore?
[/PresSteps]       -> remove (sub-page navigation for presentations) [no subpage navigation for printing / nup]
[/VP]              viewport rects -- ignore/drop or recalculate into new page

*/
QPDFObjectHandle makeXObject(QPDF *pdf,QPDFObjectHandle page)
{
  page.assertPageObject();

  QPDFObjectHandle ret=QPDFObjectHandle::newStream(pdf);
  QPDFObjectHandle dict=ret.getDict();

  dict.replaceKey("/Type",QPDFObjectHandle::newName("/XObject")); // optional
  dict.replaceKey("/Subtype",QPDFObjectHandle::newName("/Form")); // required
//  dict.replaceKey("/FormType",QPDFObjectHandle::newInteger(1)); // optional

  QPDFObjectHandle box=getTrimBox(page); // already in "form space"
  dict.replaceKey("/BBox",box); // reqd

  // [/Matrix .]   ...  default is [1 0 0 1 0 0]; we incorporate /UserUnit and /Rotate here
  Matrix mtx;
  if (page.hasKey("/UserUnit")) {
    mtx.scale(page.getKey("/UserUnit").getNumericValue());
  }

  // transform, so that bbox is [0 0 w h]  (in outer space, but after UserUnit)
  Rotation rot=getRotate(page);
  
  // calculate rotation effect on [0 0 w h]
  PageRect bbox=getBoxAsRect(box),tmp;
  tmp.left=0;
  tmp.bottom=0;
  tmp.right=0;
  tmp.top=0;
  tmp.rotate_move(rot,bbox.width,bbox.height);
  // tmp.rotate_move moves the bbox; we must achieve this move with the matrix.
  mtx.translate(tmp.left,tmp.bottom); // 1. move origin to end up at left,bottom after rotation

  mtx.rotate(rot);  // 2. rotate coordinates according to /Rotate
  mtx.translate(-bbox.left,-bbox.bottom);  // 3. move origin from 0,0 to "form space"

  dict.replaceKey("/Matrix",mtx.get());

  dict.replaceKey("/Resources",page.getKey("/Resources"));
  if (page.hasKey("/Group")) {
    dict.replaceKey("/Group",page.getKey("/Group")); // (transparency); opt, copy if there
  }

// ?? /StructParents   ... can basically copy from page, but would need fixup in Structure Tree
// FIXME: remove (globally) Tagged spec (/MarkInfo), and Structure Tree

  // Note: [/Name]  (reqd. only in 1.0 -- but there we even can't use our normal img/patter procedures)

// none:
//  QPDFObjectHandle filter=QPDFObjectHandle::newArray();
//  QPDFObjectHandle decode_parms=QPDFObjectHandle::newArray();
  // null leads to use of "default filters" from qpdf's settings
  QPDFObjectHandle filter=QPDFObjectHandle::newNull();
  QPDFObjectHandle decode_parms=QPDFObjectHandle::newNull();

  std::vector<QPDFObjectHandle> contents=page.getPageContents();  // (will assertPageObject)

  auto ph=PointerHolder<QPDFObjectHandle::StreamDataProvider>(new CombineFromContents_Provider(contents));
  ret.replaceStreamData(ph,filter,decode_parms);

  return ret;
}

/*
  we will have to fix up the structure tree (e.g. /K in element), when copying  /StructParents;
    (there is /Pg, which has to point to the containing page, /Stm when it's not part of the page's content stream 
     i.e. when it is in our XObject!; then there is /StmOwn ...)
  when not copying, we have to remove the structure tree completely (also /MarkInfo dict)
  Still this might not be sufficient(?), as there are probably BDC and EMC operators in the stream.
*/

/* /XObject /Form has
[/Type /XObject]
/Subtype /Form
[/FormType 1]
/BBox rect         from crop box, or recalculate
[/Matrix .]   ...  default is [1 0 0 1 0 0] ---   we have to incorporate /UserUnit here?!
[/Resources dict]  from page.
[/Group dict]      used for transparency -- can copy from page
[/Ref dict]        not needed; for external reference
[/Metadata]        not, as long we can not combine.
[/PieceInfo]       can copy, but not combine 
[/LastModified date]    copy if /PieceInfo there
[/StructParent]    . don't want to be one   ... have to read more spec
[/StructParents]   . copy from page!
[/OPI]             no opi version. don't set
[/OC]              is this optional content? NO! not needed.
[/Name]            (only reqd. in 1.0 -- but there we even can't use our normal img/patter procedures)
*/


