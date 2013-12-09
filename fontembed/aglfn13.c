#include <stdlib.h>

const char *aglfn13(unsigned short uni);

#ifdef WITH_AGLFN
static const char *agl_l207e[]={
  "space","exclam","quotedbl","numbersign", "dollar","percent","ampersand","quotesingle",
  "parenleft","parenright","asterisk","plus", "comma","hyphen","period","slash",
  "zero","one","two","three", "four","five","six","seven",  "eight","nine","colon","semicolon", "less","equal","greater","question",
  "at","A","B","C", "D","E","F","G",  "H","I","J","K", "L","M","N","O",
  "P","Q","R","S", "T","U","V","W",  "X","Y","Z","bracketleft", "backslash","bracketright","asciicircum","underscore",
  "grave","a","b","c", "d","e","f","g",  "h","i","j","k", "l","m","n","o",
  "p","q","r","s", "t","u","v","w",  "x","y","z","braceleft", "bar","braceright","asciitilde"};

static const char *agl_la1ff[]={
  "exclamdown","cent","sterling","currency", "yen","brokenbar","section","dieresis",
  "copyright","ordfeminine","guillemotleft","logicalnot", "registered","macron","degree","plusminus",
  0,0,"acute",0, "paragraph","periodcentered","cedilla",0,
  "ordmasculine","guillemotright","onequarter","onehalf", "threequarters","questiondown","Agrave","Aacute",
  "Acircumflex","Atilde","Adieresis","Aring", "AE","Ccedilla","Egrave","Eacute",
  "Ecircumflex","Edieresis","Igrave","Iacute", "Icircumflex","Idieresis","Eth","Ntilde",
  "Ograve","Oacute","Ocircumflex","Otilde", "Odieresis","multiply","Oslash","Ugrave",
  "Uacute","Ucircumflex","Udieresis","Yacute", "Thorn","germandbls","agrave","aacute",
  "acircumflex","atilde","adieresis","aring", "ae","ccedilla","egrave","eacute",
  "ecircumflex","edieresis","igrave","iacute", "icircumflex","idieresis","eth","ntilde",
  "ograve","oacute","ocircumflex","otilde", "odieresis","divide","oslash","ugrave",
  "uacute","ucircumflex","udieresis","yacute", "thorn","ydieresis"};

static const char *agl_l1007f[]={
  "Amacron","amacron","Abreve","abreve", "Aogonek","aogonek","Cacute","cacute",
  "Ccircumflex","ccircumflex","Cdotaccent","cdotaccent", "Ccaron","ccaron","Dcaron","dcaron",
  "Dcroat","dcroat","Emacron","emacron", "Ebreve","ebreve","Edotaccent","edotaccent",
  "Eogonek","eogonek","Ecaron","ecaron", "Gcircumflex","gcircumflex","Gbreve","gbreve",
  "Gdotaccent","gdotaccent","Gcommaaccent","gcommaaccent", "Hcircumflex","hcircumflex","Hbar","hbar",
  "Itilde","itilde","Imacron","imacron", "Ibreve","ibreve","Iogonek","iogonek",
  "Idotaccent","dotlessi","IJ","ij", "Jcircumflex","jcircumflex","Kcommaaccent","kcommaaccent",
  "kgreenlandic","Lacute","lacute","Lcommaaccent", "lcommaaccent","Lcaron","lcaron","Ldot",
  "ldot","Lslash","lslash","Nacute", "nacute","Ncommaaccent","ncommaaccent","Ncaron",
  "ncaron","napostrophe","Eng","eng", "Omacron","omacron","Obreve","obreve",
  "Ohungarumlaut","ohungarumlaut","OE","oe", "Racute","racute","Rcommaaccent","rcommaaccent",
  "Rcaron","rcaron","Sacute","sacute", "Scircumflex","scircumflex","Scedilla","scedilla",
  "Scaron","scaron","Tcommaaccent","tcommaaccent", "Tcaron","tcaron","Tbar","tbar",
  "Utilde","utilde","Umacron","umacron", "Ubreve","ubreve","Uring","uring",
  "Uhungarumlaut","uhungarumlaut","Uogonek","uogonek", "Wcircumflex","wcircumflex","Ycircumflex","ycircumflex",
  "Ydieresis","Zacute","zacute","Zdotaccent", "zdotaccent","Zcaron","zcaron","longs"};

struct agl_lt { unsigned short uid; const char *name; } agl_lxx[]={
  {0x0192,"florin"},{0x01A0,"Ohorn"},{0x01A1,"ohorn"},{0x01AF,"Uhorn"},
  {0x01B0,"uhorn"},{0x01E6,"Gcaron"},{0x01E7,"gcaron"},{0x01FA,"Aringacute"},
  {0x01FB,"aringacute"},{0x01FC,"AEacute"},{0x01FD,"aeacute"},{0x01FE,"Oslashacute"},
  {0x01FF,"oslashacute"},{0x0218,"Scommaaccent"},{0x0219,"scommaaccent"},{0x02BC,"afii57929"},
  {0x02BD,"afii64937"},{0x02C6,"circumflex"},{0x02C7,"caron"},{0x02D8,"breve"},
  {0x02D9,"dotaccent"},{0x02DA,"ring"},{0x02DB,"ogonek"},{0x02DC,"tilde"},
  {0x02DD,"hungarumlaut"},{0x0300,"gravecomb"},{0x0301,"acutecomb"},{0x0303,"tildecomb"},
  {0x0309,"hookabovecomb"},{0x0323,"dotbelowcomb"},{0x0384,"tonos"},{0x0385,"dieresistonos"},
  {0x0386,"Alphatonos"},{0x0387,"anoteleia"},{0x0388,"Epsilontonos"},{0x0389,"Etatonos"},
  {0x038A,"Iotatonos"},{0x038C,"Omicrontonos"},{0x038E,"Upsilontonos"},{0x038F,"Omegatonos"},
  {0x0390,"iotadieresistonos"},{0x0391,"Alpha"},{0x0392,"Beta"},{0x0393,"Gamma"},
  {0x0394,"Delta"},{0x0395,"Epsilon"},{0x0396,"Zeta"},{0x0397,"Eta"},
  {0x0398,"Theta"},{0x0399,"Iota"},{0x039A,"Kappa"},{0x039B,"Lambda"},
  {0x039C,"Mu"},{0x039D,"Nu"},{0x039E,"Xi"},{0x039F,"Omicron"},
  {0x03A0,"Pi"},{0x03A1,"Rho"},{0x03A3,"Sigma"},{0x03A4,"Tau"},
  {0x03A5,"Upsilon"},{0x03A6,"Phi"},{0x03A7,"Chi"},{0x03A8,"Psi"},
  {0x03A9,"Omega"},{0x03AA,"Iotadieresis"},{0x03AB,"Upsilondieresis"},{0x03AC,"alphatonos"},
  {0x03AD,"epsilontonos"},{0x03AE,"etatonos"},{0x03AF,"iotatonos"},{0x03B0,"upsilondieresistonos"},
  {0x03B1,"alpha"},{0x03B2,"beta"},{0x03B3,"gamma"},{0x03B4,"delta"},
  {0x03B5,"epsilon"},{0x03B6,"zeta"},{0x03B7,"eta"},{0x03B8,"theta"},
  {0x03B9,"iota"},{0x03BA,"kappa"},{0x03BB,"lambda"},{0x03BC,"mu"},
  {0x03BD,"nu"},{0x03BE,"xi"},{0x03BF,"omicron"},{0x03C0,"pi"},
  {0x03C1,"rho"},{0x03C2,"sigma1"},{0x03C3,"sigma"},{0x03C4,"tau"},
  {0x03C5,"upsilon"},{0x03C6,"phi"},{0x03C7,"chi"},{0x03C8,"psi"},
  {0x03C9,"omega"},{0x03CA,"iotadieresis"},{0x03CB,"upsilondieresis"},
  {0x03CC,"omicrontonos"},{0x03CD,"upsilontonos"},{0x03CE,"omegatonos"},
  {0x03D1,"theta1"},{0x03D2,"Upsilon1"},{0x03D5,"phi1"},{0x03D6,"omega1"},
  {0x0401,"afii10023"},{0x0402,"afii10051"},{0x0403,"afii10052"},{0x0404,"afii10053"},
  {0x0405,"afii10054"},{0x0406,"afii10055"},{0x0407,"afii10056"},{0x0408,"afii10057"},
  {0x0409,"afii10058"},{0x040A,"afii10059"},{0x040B,"afii10060"},{0x040C,"afii10061"},
  {0x040E,"afii10062"},{0x040F,"afii10145"},{0x0410,"afii10017"},{0x0411,"afii10018"},
  {0x0412,"afii10019"},{0x0413,"afii10020"},{0x0414,"afii10021"},{0x0415,"afii10022"},
  {0x0416,"afii10024"},{0x0417,"afii10025"},{0x0418,"afii10026"},{0x0419,"afii10027"},
  {0x041A,"afii10028"},{0x041B,"afii10029"},{0x041C,"afii10030"},{0x041D,"afii10031"},
  {0x041E,"afii10032"},{0x041F,"afii10033"},{0x0420,"afii10034"},{0x0421,"afii10035"},
  {0x0422,"afii10036"},{0x0423,"afii10037"},{0x0424,"afii10038"},{0x0425,"afii10039"},
  {0x0426,"afii10040"},{0x0427,"afii10041"},{0x0428,"afii10042"},{0x0429,"afii10043"},
  {0x042A,"afii10044"},{0x042B,"afii10045"},{0x042C,"afii10046"},{0x042D,"afii10047"},
  {0x042E,"afii10048"},{0x042F,"afii10049"},{0x0430,"afii10065"},{0x0431,"afii10066"},
  {0x0432,"afii10067"},{0x0433,"afii10068"},{0x0434,"afii10069"},{0x0435,"afii10070"},
  {0x0436,"afii10072"},{0x0437,"afii10073"},{0x0438,"afii10074"},{0x0439,"afii10075"},
  {0x043A,"afii10076"},{0x043B,"afii10077"},{0x043C,"afii10078"},{0x043D,"afii10079"},
  {0x043E,"afii10080"},{0x043F,"afii10081"},{0x0440,"afii10082"},{0x0441,"afii10083"},
  {0x0442,"afii10084"},{0x0443,"afii10085"},{0x0444,"afii10086"},{0x0445,"afii10087"},
  {0x0446,"afii10088"},{0x0447,"afii10089"},{0x0448,"afii10090"},{0x0449,"afii10091"},
  {0x044A,"afii10092"},{0x044B,"afii10093"},{0x044C,"afii10094"},{0x044D,"afii10095"},
  {0x044E,"afii10096"},{0x044F,"afii10097"},{0x0451,"afii10071"},{0x0452,"afii10099"},
  {0x0453,"afii10100"},{0x0454,"afii10101"},{0x0455,"afii10102"},{0x0456,"afii10103"},
  {0x0457,"afii10104"},{0x0458,"afii10105"},{0x0459,"afii10106"},{0x045A,"afii10107"},
  {0x045B,"afii10108"},{0x045C,"afii10109"},{0x045E,"afii10110"},{0x045F,"afii10193"},
  {0x0462,"afii10146"},{0x0463,"afii10194"},{0x0472,"afii10147"},{0x0473,"afii10195"},
  {0x0474,"afii10148"},{0x0475,"afii10196"},{0x0490,"afii10050"},{0x0491,"afii10098"},
  {0x04D9,"afii10846"},{0x05B0,"afii57799"},{0x05B1,"afii57801"},{0x05B2,"afii57800"},
  {0x05B3,"afii57802"},{0x05B4,"afii57793"},{0x05B5,"afii57794"},{0x05B6,"afii57795"},
  {0x05B7,"afii57798"},{0x05B8,"afii57797"},{0x05B9,"afii57806"},{0x05BB,"afii57796"},
  {0x05BC,"afii57807"},{0x05BD,"afii57839"},{0x05BE,"afii57645"},{0x05BF,"afii57841"},
  {0x05C0,"afii57842"},{0x05C1,"afii57804"},{0x05C2,"afii57803"},{0x05C3,"afii57658"},
  {0x05D0,"afii57664"},{0x05D1,"afii57665"},{0x05D2,"afii57666"},{0x05D3,"afii57667"},
  {0x05D4,"afii57668"},{0x05D5,"afii57669"},{0x05D6,"afii57670"},{0x05D7,"afii57671"},
  {0x05D8,"afii57672"},{0x05D9,"afii57673"},{0x05DA,"afii57674"},{0x05DB,"afii57675"},
  {0x05DC,"afii57676"},{0x05DD,"afii57677"},{0x05DE,"afii57678"},{0x05DF,"afii57679"},
  {0x05E0,"afii57680"},{0x05E1,"afii57681"},{0x05E2,"afii57682"},{0x05E3,"afii57683"},
  {0x05E4,"afii57684"},{0x05E5,"afii57685"},{0x05E6,"afii57686"},{0x05E7,"afii57687"},
  {0x05E8,"afii57688"},{0x05E9,"afii57689"},{0x05EA,"afii57690"},{0x05F0,"afii57716"},
  {0x05F1,"afii57717"},{0x05F2,"afii57718"},{0x060C,"afii57388"},{0x061B,"afii57403"},
  {0x061F,"afii57407"},{0x0621,"afii57409"},{0x0622,"afii57410"},{0x0623,"afii57411"},
  {0x0624,"afii57412"},{0x0625,"afii57413"},{0x0626,"afii57414"},{0x0627,"afii57415"},
  {0x0628,"afii57416"},{0x0629,"afii57417"},{0x062A,"afii57418"},{0x062B,"afii57419"},
  {0x062C,"afii57420"},{0x062D,"afii57421"},{0x062E,"afii57422"},{0x062F,"afii57423"},
  {0x0630,"afii57424"},{0x0631,"afii57425"},{0x0632,"afii57426"},{0x0633,"afii57427"},
  {0x0634,"afii57428"},{0x0635,"afii57429"},{0x0636,"afii57430"},{0x0637,"afii57431"},
  {0x0638,"afii57432"},{0x0639,"afii57433"},{0x063A,"afii57434"},{0x0640,"afii57440"},
  {0x0641,"afii57441"},{0x0642,"afii57442"},{0x0643,"afii57443"},{0x0644,"afii57444"},
  {0x0645,"afii57445"},{0x0646,"afii57446"},{0x0647,"afii57470"},{0x0648,"afii57448"},
  {0x0649,"afii57449"},{0x064A,"afii57450"},{0x064B,"afii57451"},{0x064C,"afii57452"},
  {0x064D,"afii57453"},{0x064E,"afii57454"},{0x064F,"afii57455"},{0x0650,"afii57456"},
  {0x0651,"afii57457"},{0x0652,"afii57458"},{0x0660,"afii57392"},{0x0661,"afii57393"},
  {0x0662,"afii57394"},{0x0663,"afii57395"},{0x0664,"afii57396"},{0x0665,"afii57397"},
  {0x0666,"afii57398"},{0x0667,"afii57399"},{0x0668,"afii57400"},{0x0669,"afii57401"},
  {0x066A,"afii57381"},{0x066D,"afii63167"},{0x0679,"afii57511"},{0x067E,"afii57506"},
  {0x0686,"afii57507"},{0x0688,"afii57512"},{0x0691,"afii57513"},{0x0698,"afii57508"},
  {0x06A4,"afii57505"},{0x06AF,"afii57509"},{0x06BA,"afii57514"},{0x06D2,"afii57519"},
  {0x06D5,"afii57534"},{0x1E80,"Wgrave"},{0x1E81,"wgrave"},{0x1E82,"Wacute"},
  {0x1E83,"wacute"},{0x1E84,"Wdieresis"},{0x1E85,"wdieresis"},{0x1EF2,"Ygrave"},
  {0x1EF3,"ygrave"},{0x200C,"afii61664"},{0x200D,"afii301"},{0x200E,"afii299"},
  {0x200F,"afii300"},{0x2012,"figuredash"},{0x2013,"endash"},{0x2014,"emdash"},
  {0x2015,"afii00208"},{0x2017,"underscoredbl"},{0x2018,"quoteleft"},{0x2019,"quoteright"},
  {0x201A,"quotesinglbase"},{0x201B,"quotereversed"},{0x201C,"quotedblleft"},{0x201D,"quotedblright"},
  {0x201E,"quotedblbase"},{0x2020,"dagger"},{0x2021,"daggerdbl"},{0x2022,"bullet"},
  {0x2024,"onedotenleader"},{0x2025,"twodotenleader"},{0x2026,"ellipsis"},{0x202C,"afii61573"},
  {0x202D,"afii61574"},{0x202E,"afii61575"},{0x2030,"perthousand"},{0x2032,"minute"},
  {0x2033,"second"},{0x2039,"guilsinglleft"},{0x203A,"guilsinglright"},{0x203C,"exclamdbl"},
  {0x2044,"fraction"},{0x20A1,"colonmonetary"},{0x20A3,"franc"},{0x20A4,"lira"},
  {0x20A7,"peseta"},{0x20AA,"afii57636"},{0x20AB,"dong"},{0x20AC,"Euro"},
  {0x2105,"afii61248"},{0x2111,"Ifraktur"},{0x2113,"afii61289"},{0x2116,"afii61352"},
  {0x2118,"weierstrass"},{0x211C,"Rfraktur"},{0x211E,"prescription"},{0x2122,"trademark"},
  {0x212E,"estimated"},{0x2135,"aleph"},{0x2153,"onethird"},{0x2154,"twothirds"},
  {0x215B,"oneeighth"},{0x215C,"threeeighths"},{0x215D,"fiveeighths"},{0x215E,"seveneighths"},
  {0x2190,"arrowleft"},{0x2191,"arrowup"},{0x2192,"arrowright"},{0x2193,"arrowdown"},
  {0x2194,"arrowboth"},{0x2195,"arrowupdn"},{0x21A8,"arrowupdnbse"},{0x21B5,"carriagereturn"},
  {0x21D0,"arrowdblleft"},{0x21D1,"arrowdblup"},{0x21D2,"arrowdblright"},{0x21D3,"arrowdbldown"},
  {0x21D4,"arrowdblboth"},{0x2200,"universal"},{0x2202,"partialdiff"},{0x2203,"existential"},
  {0x2205,"emptyset"},{0x2207,"gradient"},{0x2208,"element"},{0x2209,"notelement"},
  {0x220B,"suchthat"},{0x220F,"product"},{0x2211,"summation"},{0x2212,"minus"},
  {0x2217,"asteriskmath"},{0x221A,"radical"},{0x221D,"proportional"},{0x221E,"infinity"},
  {0x221F,"orthogonal"},{0x2220,"angle"},{0x2227,"logicaland"},{0x2228,"logicalor"},
  {0x2229,"intersection"},{0x222A,"union"},{0x222B,"integral"},{0x2234,"therefore"},
  {0x223C,"similar"},{0x2245,"congruent"},{0x2248,"approxequal"},{0x2260,"notequal"},
  {0x2261,"equivalence"},{0x2264,"lessequal"},{0x2265,"greaterequal"},{0x2282,"propersubset"},
  {0x2283,"propersuperset"},{0x2284,"notsubset"},{0x2286,"reflexsubset"},{0x2287,"reflexsuperset"},
  {0x2295,"circleplus"},{0x2297,"circlemultiply"},{0x22A5,"perpendicular"},{0x22C5,"dotmath"},
  {0x2302,"house"},{0x2310,"revlogicalnot"},{0x2320,"integraltp"},{0x2321,"integralbt"},
  {0x2329,"angleleft"},{0x232A,"angleright"},{0x2500,"SF100000"},{0x2502,"SF110000"},
  {0x250C,"SF010000"},{0x2510,"SF030000"},{0x2514,"SF020000"},{0x2518,"SF040000"},
  {0x251C,"SF080000"},{0x2524,"SF090000"},{0x252C,"SF060000"},{0x2534,"SF070000"},
  {0x253C,"SF050000"},{0x2550,"SF430000"},{0x2551,"SF240000"},{0x2552,"SF510000"},
  {0x2553,"SF520000"},{0x2554,"SF390000"},{0x2555,"SF220000"},{0x2556,"SF210000"},
  {0x2557,"SF250000"},{0x2558,"SF500000"},{0x2559,"SF490000"},{0x255A,"SF380000"},
  {0x255B,"SF280000"},{0x255C,"SF270000"},{0x255D,"SF260000"},{0x255E,"SF360000"},
  {0x255F,"SF370000"},{0x2560,"SF420000"},{0x2561,"SF190000"},{0x2562,"SF200000"},
  {0x2563,"SF230000"},{0x2564,"SF470000"},{0x2565,"SF480000"},{0x2566,"SF410000"},
  {0x2567,"SF450000"},{0x2568,"SF460000"},{0x2569,"SF400000"},{0x256A,"SF540000"},
  {0x256B,"SF530000"},{0x256C,"SF440000"},{0x2580,"upblock"},{0x2584,"dnblock"},
  {0x2588,"block"},{0x258C,"lfblock"},{0x2590,"rtblock"},{0x2591,"ltshade"},
  {0x2592,"shade"},{0x2593,"dkshade"},{0x25A0,"filledbox"},{0x25A1,"H22073"},
  {0x25AA,"H18543"},{0x25AB,"H18551"},{0x25AC,"filledrect"},{0x25B2,"triagup"},
  {0x25BA,"triagrt"},{0x25BC,"triagdn"},{0x25C4,"triaglf"},{0x25CA,"lozenge"},
  {0x25CB,"circle"},{0x25CF,"H18533"},{0x25D8,"invbullet"},{0x25D9,"invcircle"},
  {0x25E6,"openbullet"},{0x263A,"smileface"},{0x263B,"invsmileface"},{0x263C,"sun"},
  {0x2640,"female"},{0x2642,"male"},{0x2660,"spade"},{0x2663,"club"},
  {0x2665,"heart"},{0x2666,"diamond"},{0x266A,"musicalnote"},{0x266B,"musicalnotedbl"}};

static int agl_cmp(const void *a,const void *b)
{
  const unsigned short aa=((struct agl_lt *)a)->uid,bb=((struct agl_lt *)b)->uid;
  if (aa<bb) {
    return -1;
  } else if (aa>bb) {
    return 1;
  }
  return 0;
}

const char *aglfn13(unsigned short uni)
{
  if ( (uni>=0x0020)&&(uni<0x007f) ) {
    return agl_l207e[uni-0x0020];
  } else if ( (uni>=0x00a1)&&(uni<=0x00ff) ) {
    return agl_la1ff[uni-0x00a1];
  } else if ( (uni>=0x0100)&&(uni<=0x017f) ) {
    return agl_l1007f[uni-0x0100];
  } else if (uni>=0x0180) {
    struct agl_lt key,*res;
    key.uid=uni;
    res=bsearch(&key,agl_lxx,(sizeof(agl_lxx)/sizeof(struct agl_lt)),sizeof(struct agl_lt),agl_cmp);
    if (res) {
      return res->name;
    }
  }
  return NULL;
}
#else
const char *aglfn13(unsigned short uni)
{
  return NULL;
}
#endif
