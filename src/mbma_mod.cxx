/*
  $Id$
  $URL$

  Copyright (c) 2006 - 2012
  Tilburg University

  This file is part of frog.

  frog is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  frog is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  For questions and suggestions, see:
      http://ilk.uvt.nl/software.html
  or send mail to:
      timbl@uvt.nl
*/

#include <cstdlib>
#include <string>
#include <set>
#include <iostream>
#include <fstream>
#include <timbl/TimblAPI.h>

#include "frog/Frog.h"
#include "frog/Configuration.h"
#include "libfolia/folia.h"
#include "libfolia/document.h"
#include "frog/mbma_mod.h"

using namespace std;
using namespace folia;

const long int LEFT =  6; // left context
const long int RIGHT = 6; // right context

Mbma::Mbma(): MTreeFilename( "dm.igtree" ), MTree(0) { 
  mbmaLog = new LogStream( theErrLog, "mbma-" );
}

Mbma::~Mbma() { 
  cleanUp(); 
  delete mbmaLog;
};

void Mbma::fillMaps() {
  //
  // this could be done from a configfile
  //
  // first the CELEX POS tag names
  //
  tagNames["N"] = "noun";
  tagNames["A"] = "adjective";
  tagNames["Q"] = "quantifier/numeral";
  tagNames["V"] = "verb";
  tagNames["D"] = "article";
  tagNames["O"] = "pronoun";
  tagNames["B"] = "adverb";
  tagNames["P"] = "preposition";
  tagNames["Y"] = "conjunction";
  tagNames["I"] = "interjection";
  tagNames["X"] = "unanalysed";
  tagNames["Z"] = "expression part";
  tagNames["PN"] = "proper noun";
  //
  // now the inflections
  iNames['X'] = "";
  iNames['s'] = "separated";
  iNames['e'] = "singular";
  iNames['m'] = "plural";
  iNames['d'] = "diminutive";
  iNames['G'] = "genitive";
  iNames['D'] = "dative";
  iNames['P'] = "positive";
  iNames['C'] = "comparative";
  iNames['S'] = "superlative";
  iNames['E'] = "suffix-e";
  iNames['i'] = "infinitive";
  iNames['p'] = "participle";
  iNames['t'] = "present tense";
  iNames['v'] = "past tense";
  iNames['1'] = "1st person";
  iNames['2'] = "2nd person";
  iNames['3'] = "3rd person";
  iNames['I'] = "inversed";
  iNames['g'] = "imperative";
  iNames['a'] = "subjunctive";
}

// BJ: dirty hack with fixed everything to read in tag correspondences
void Mbma::init_cgn( const string& dir ) {
  string line;
  string fn = dir + "cgntags.main";
  ifstream tc( fn.c_str() );
  if ( tc ){
    while( getline( tc, line) ) {
      vector<string> tmp;
      size_t num = split_at(line, tmp, " ");
      if ( num < 2 ){
	*Log(mbmaLog) << "splitting '" << line << "' failed" << endl;
	throw ( runtime_error("panic") );
      }
      TAGconv.insert( make_pair( tmp[0], tmp[1] ) );
    }
  }
  else
    throw ( runtime_error( "unable to open:" + fn ) );
  fn = dir + "cgntags.sub";
  ifstream tc1( fn.c_str() );
  if ( tc1 ){
    while( getline(tc1, line) ) {
      vector<string> tmp;
      size_t num = split_at(line, tmp, " ");
      if ( num == 2 )
	TAGconv.insert( make_pair( tmp[0], tmp[1] ) ); 
    }
  }
  else
    throw ( runtime_error( "unable to open:" + fn ) );
}
  
bool Mbma::init( const Configuration& config ) {
  *Log(mbmaLog) << "Initiating morphological analyzer..." << endl;
  debugFlag = tpDebug;
  string db = config.lookUp( "debug", "mbma" );
  if ( !db.empty() )
    debugFlag = stringTo<int>( db );
  string val = config.lookUp( "version", "mbma" );
  if ( val.empty() ){
    version = "1.0";
  }
  else
    version = val;
  val = config.lookUp( "set", "mbma" );
  if ( val.empty() ){
    tagset = "http://ilk.uvt.nl/folia/sets/frog-mbma-nl";
  }
  else
    tagset = val;
  string tfName = config.lookUp( "treeFile", "mbma" );
  if ( tfName.empty() )
    tfName = "mbma.igtree";
  MTreeFilename = prefix( config.configDir(), tfName );
  fillMaps();
  init_cgn( config.configDir() );
    //Read in (igtree) data
  string opts = config.lookUp( "timblOpts", "mbma" );
  if ( opts.empty() )
    opts = "-a1";
  opts += " +vs -vf"; // make Timbl run quietly
  MTree = new Timbl::TimblAPI(opts);
  return MTree->GetInstanceBase(MTreeFilename);
}
  
void Mbma::cleanUp(){
  // *Log(mbmaLog) << "cleaning up MBMA stuff " << endl;
  delete MTree;
  MTree = 0;
}
  
vector<string> Mbma::make_instances( const UnicodeString& word ){
  vector<string> insts;
  if (debugFlag > 2)
    *Log(mbmaLog) << "word: " << word << "\twl : " << word.length() << endl;
  for( long i=0; i < word.length(); ++i ) {
    if (debugFlag > 10)
      *Log(mbmaLog) << "itt #:" << i << endl;
    UnicodeString inst;
    for ( long j=i ; j <= i + RIGHT + LEFT; ++j ) {
      if (debugFlag > 10)
	*Log(mbmaLog) << " " << j-LEFT << ": ";
      if ( j < LEFT || j >= word.length()+LEFT )
	inst += '_';
      else {
	if (word[j-LEFT] == ',' )
	  inst += 'C';
	else
	  inst += word[j-LEFT];
      }
      inst += ",";
      if (debugFlag > 10)
	*Log(mbmaLog) << " : " << inst << endl;
    }
    inst += "?";
    if (debugFlag > 2)
      *Log(mbmaLog) << "inst #" << i << " : " << inst << endl;
    // classify res
    insts.push_back( UnicodeToUTF8(inst) );
    // store res
  }
  return insts;
}
  
string extract( const string& line, size_t start, char to ){
  return line.substr( start, line.find( to , start ) - start );
}

string find_class( unsigned int step, 
		   const vector<string>& classes,
		   unsigned int nranal ){
  string result = classes[0];
  if ( nranal > 1 ){
    if ( classes.size() > 1 ){
      if ( classes.size() >= step )
	result = classes[step-1];
      else
	result = "0";
    }
  }
  return result;
}
  
string Mbma::calculate_ins_del( const string& in_class, 
				string& deletestring,
				string& insertstring ){
  string result_class = in_class;
  size_t pos = in_class.find("+");
  if ( pos != string::npos ) { 
    if ( debugFlag){
      *Log(mbmaLog) << "calculate ins/del for " << in_class << endl;
    }
    pos++;
    if (in_class[pos]=='D') { // delete operation 
      deletestring = extract( in_class, pos+1, '/' );
    }
    else if ( in_class[pos]=='I') {  //insert operation
      insertstring = extract( in_class, pos+1, '/' );
    }
    else if ( in_class[pos]=='R') { // replace operation 
      deletestring = extract( in_class, pos+1, '>' );
      pos += deletestring.length()+1;
      insertstring = extract( in_class, pos+1, '/' );
    }
    /* spelling change done; remove from in_class */
    result_class = extract( in_class, 0, '+' );
    pos = in_class.find("/");
    if ( pos != string::npos )
      result_class += in_class.substr( pos );
    if ( debugFlag){
      *Log(mbmaLog) << "Insert = " << insertstring << ", delete=" << deletestring << endl;
      *Log(mbmaLog) << "result class=" << result_class << endl;
    }
  }
  else {
    if ( debugFlag ){
      *Log(mbmaLog) << "calculate_ins_del: do nothing" << endl;
    }
  }
  return result_class;
}


ostream& operator<< ( ostream& os, const waStruct& a ){
  os << "[" << a.word << "]" << a.act;
  return os;
}

ostream& operator<< ( ostream& os, const vector<waStruct>& V ){
  for ( size_t i=0; i < V.size(); ++i )
    os << V[i];
  return os;
}

vector<waStruct> Mbma::Step1( unsigned int step, 
			      const UnicodeString& word, int nranal,
			      const vector<vector<string> >& classParts,
			      const string& basictags ) {
  vector<waStruct> ana;
  size_t tobeignored=0;
  string this_class;
  string previoustag;
  waStruct waItem;
  for ( long k=0; k < word.length(); ++k ) { 
    this_class = find_class( step, classParts[k], nranal );
    if ( debugFlag){
      *Log(mbmaLog) << "Step::" << step << " " << this_class << endl;
    }
    string deletestring;
    string insertstring;
    this_class = calculate_ins_del( this_class, deletestring, insertstring);
    if ( deletestring == "eeer" )
      deletestring = "eer";
    /* exceptions */
    bool eexcept = false;
    if ( deletestring == "ere" ){ 
      deletestring = "er";
      eexcept = true;
    }
    // insert the deletestring :-) 
    UnicodeString last = UTF8ToUnicode( deletestring );
    waItem.word += last;
    // delete the insertstring :-) 
    if (( tobeignored == 0 ) &&
	( insertstring != "ge" ) &&
	( insertstring != "be" ) )
      tobeignored = insertstring.length();
    
    if ( basictags.find(this_class[0]) != string::npos &&
	 this_class != "PE" ) { 
      // encountering POS tag
      if ( !previoustag.empty() ) { 
	waItem.act = previoustag;
	ana.push_back( waItem );
	waItem.clear();
      }
      previoustag = this_class;
    }
    else { 
      if ( this_class[0] !='0' ){ 
	if ( !previoustag.empty() ) { 
	  waItem.act = previoustag;
	  ana.push_back( waItem );
	  waItem.clear();
	}
	previoustag = "i" + this_class;
      }
    }
    /* copy the next character */
    if ( eexcept ) { 
      waItem.word += "e";
      eexcept = false;
    }
    if ( tobeignored == 0 ) { 
      waItem.word += word[k];
    }
    else if (tobeignored > 0 )
      tobeignored--;
  }
  waItem.act = previoustag;
  ana.push_back( waItem );
  waItem.clear();
  
  // do we have inflection?
  size_t pos = this_class.find( '/' );
  if ( pos != string::npos && this_class != "E/P" ) { 
    string inflection = "i" + this_class.substr( pos+1 );
    waItem.act = inflection;
    ana.push_back( waItem );
  }
  return ana;
}

string change_tag( const char ch ){
  string newtag;
  switch( ch ){
  case 'm':
  case 'e':
  case 'd':
  case 'G':
  case 'D':
    newtag = "N";
  break;
  case 'P':
  case 'C':
  case 'S':
  case 'E':
    newtag = "A";
  break;
  case 'i':
  case 'p':
  case 't':
  case 'v':
  case 'g':
  case 'a':
    newtag = "V";
  break;
  default:
    break;
  }
  return newtag;
}

void Mbma::resolve_inflections( vector<waStruct>& ana, 
				const string& basictags ) {
  // resolve all clearly resolvable implicit selections of inflectional tags
  vector<waStruct>::iterator prev = ana.begin();
  vector<waStruct>::const_iterator it = ana.begin();
  int step = 0;
  while ( it != ana.end() ){
    string this_class = it->act;
    if ( step > 1 ){
      ++prev;
    }
    ++it;
    ++step;
    if ( this_class[0]=='i') { 
      if (debugFlag){
	*Log(mbmaLog) << "thisclass >" << this_class << "<" << endl;
      }	  
      /* given the specific selections of certain inflections,
	 change the tag!  */
      string newtag = change_tag( this_class[1] );
      
      /* apply the change. Remember, the idea is that an inflection is
	 far more certain of the tag of its predecessing morpheme than
	 the morpheme itself. This is not always the case, but it works  */
      if ( !newtag.empty() ) {
	if ( debugFlag  ){
	  *Log(mbmaLog) << "selects " << newtag << endl;
	}
	/* go left */
	if ( basictags.find(prev->act[0]) != string::npos ){
	  prev->act[0] = newtag[0];
	}
      }
    }
  }
}

MBMAana Mbma::addInflect( const vector<waStruct>& ana,
			  const string& inflect, 
			  const vector<string>& morph ){
  bool found = false;
  string the_act;
  vector<waStruct>::const_reverse_iterator it = ana.rbegin();
  while ( !found && it != ana.rend() ) { 
    // go back to the last non-inflectional tag 
    the_act = it->act;
    if (debugFlag){
      *Log(mbmaLog) << "final tag " << the_act << endl;
    }
    if ( the_act[0] != 'i' )
      found=true;
    else { 
      ++it;
    }
  }
  string::size_type pos = the_act.find_first_of("_/");
  string tag;
  if ( pos != string::npos )
    tag = the_act.substr( 0, pos );
  else 
    tag = the_act;
  string outTag, descr;
  map<string,string>::const_iterator tit = tagNames.find( tag ); 
  if ( tit == tagNames.end() ){
    // unknown tag
    if (debugFlag)
      *Log(mbmaLog) << "added X 4" << endl;
    outTag = "X";
    descr = "unknown";
  }
  else {
    if (debugFlag)
      *Log(mbmaLog) << "added (4) " << tit->second << endl;
    outTag = tit->first;
    descr = tit->second;
  }
  return MBMAana( outTag, inflect, morph, descr );
}

MBMAana Mbma::inflectAndAffix( const vector<waStruct>& ana ){
  string inflect;
  vector<string> morphemes;
  vector<waStruct>::const_iterator it = ana.begin();
  while ( it != ana.end() ) { 
    if ( !it->word.isEmpty() ){
      morphemes.push_back( UnicodeToUTF8(it->word) );
    }
    string this_act= it->act;
    if (debugFlag)
      *Log(mbmaLog) << "unpacking act "<< this_act << endl;
    if ( inflect.empty() && !this_act.empty() &&
	 this_act.find("_") == string::npos &&
	 this_act[0]=='i' ){
      for ( size_t i=1; i< this_act.length(); ++i ) {
	if (this_act[i]!='/') {
	  // check if it is a known inflection
	  map<char,string>::const_iterator csIt = iNames.find(this_act[i]);
	  if ( csIt == iNames.end() ){
	    if (debugFlag)
	      *Log(mbmaLog) << "added X 1" << endl;
	    inflect += "X";
	  }
	  else {
	    if (debugFlag)
	      *Log(mbmaLog) << "added (1) (" << csIt->first << ") " << csIt->second << endl;
	    inflect += this_act[i];
	  }
	}
      }
      if ( debugFlag )
	*Log(mbmaLog) << "found inflection " << inflect << endl;
    }
    if (debugFlag)	
      *Log(mbmaLog) << "morphemes now: |" << morphemes << "|" << endl;
    ++it;
  }
  if (debugFlag)	
    *Log(mbmaLog) << "inflectAndAffix: " << inflect << " - " << morphemes << endl;
  if ( inflect.empty() ){
    if (debugFlag)
      *Log(mbmaLog) << "Inflection: none " << endl;
    MBMAana retVal;
    return retVal;
  }
  else {
    // go back to the last non-inflectional
    // tag and stick it at the end
    MBMAana retVal = addInflect( ana, inflect, morphemes );
    if (debugFlag)
      *Log(mbmaLog) << "Inflection: " << retVal << endl;
    return retVal;
  }
}

void Mbma::execute( const UnicodeString& word, 
		    const vector<string>& classes ){
  const string basictags = "NAQVDOBPYIXZ";
  analysis.clear();
  // determine all alternative analyses, remember the largest
  // and store every part in a vector of string vectors
  int largest_anal=1;
  vector<vector<string> > classParts;
  classParts.resize( classes.size() );
  for ( unsigned int j=0; j< classes.size(); ++j ){
    vector<string> parts;
    int num = split_at( classes[j], parts, "|" );
    if ( num > 0 ){
      classParts[j] = parts;
      if ( num > largest_anal )
	largest_anal = num;
    }
    else {
      // only one, create a dummy
      vector<string> dummy;
      dummy.push_back( classes[j] );
      classParts[j] = dummy;
    }
  }
  
  if (debugFlag){
    string out = "alternatives: word=" + UnicodeToUTF8(word) + ", classes=<";
    for ( size_t i=0; i < classes.size(); ++i )
      out += classes[i] + ",";
    out += ">";
    *Log(mbmaLog) << out << endl;
  }    
  
  // now loop over all the analysis
  for ( unsigned int step=largest_anal; step> 0; --step ) { 
    vector<waStruct> ana = Step1( step, word, largest_anal, 
				  classParts, basictags );
    if (debugFlag)
      *Log(mbmaLog) << "intermediate analysis 1: " << ana << endl;
    resolve_inflections( ana, basictags );
    /* finally, unpack the tag and inflection names to make everything readable */
    if (debugFlag)
      *Log(mbmaLog) << "intermediate analysis 3: " << ana << endl;
    MBMAana tmp = inflectAndAffix( ana );
    analysis.push_back( tmp );
  }
}

void Mbma::addMorph( Word *word, 
		     const vector<string>& lemmas ){
  MorphologyLayer *ml = new MorphologyLayer();
#pragma omp critical(foliaupdate)
  {
    word->append( ml );
  }
  int offset = 0;
  for ( size_t p=0; p < lemmas.size(); ++p ){
    Morpheme *m = new Morpheme();
#pragma omp critical(foliaupdate)
    {
      ml->append( m );
    }
    KWargs args;
    args["value"] = lemmas[p];
    args["offset"] = toString(offset);
    TextContent *t = new TextContent( args );
    offset += lemmas[p].length();
#pragma omp critical(foliaupdate)
    {
      m->append( t );
    }
  }
}	  
      
void Mbma::postprocess( Word *fword, PosAnnotation *pos ){
  if (debugFlag){
    for(vector<MBMAana>::const_iterator it=analysis.begin(); it != analysis.end(); it++)
      *Log(mbmaLog) << "tag " <<  it->getTag() + " inflection: " << it->getInflection() << endl;
    *Log(mbmaLog) << "morph analyses: " << endl;;
    for (vector<MBMAana>::const_iterator it=analysis.begin(); it != analysis.end(); it++)
      *Log(mbmaLog) << it->getMorph() << endl;
    *Log(mbmaLog) << "before morpho: " << endl;
  }
  const string tag = pos->feat("head");
  map<string,string>::const_iterator tagIt = TAGconv.find( tag );
  if ( tagIt == TAGconv.end() ) {
    //
    // this should never happen
    //
    if (debugFlag){
      *Log(mbmaLog) << "no match!" << endl;
    } 
    throw ValueError( "unknown pos tag value '" + tag + "'" );
  }
  else {
    if (debugFlag){
      *Log(mbmaLog) << "#matches: " << tagIt->first << " " << tagIt->second << endl;
    }
    size_t match = 0;	
    vector<size_t> matches;
    for ( size_t m_index = 0; m_index <analysis.size(); ++m_index ) {
      if (debugFlag)
	*Log(mbmaLog) << "comparing " << tagIt->second << " with " << analysis[m_index].getTag() << endl;
      if ( tagIt->second == analysis[m_index].getTag() ) {
	match++;
	matches.push_back( m_index );
      }
    }
    if (debugFlag) {
      *Log(mbmaLog) << "main tag " << tagIt->second
		    << " matches " << match 
		    << " morpho analyses: " << endl;
      for( size_t p=0; p < match; ++p )  {
	*Log(mbmaLog) << "\tmatch #" << p << " : " << analysis[matches[p]].getMorph() << endl;
      }
    }
    //should be(come?) a switch
    if ( match == 0 ) {
      // fallback option: use the word and pretend it's a lemma ;-)
      UnicodeString word;
#pragma omp critical(foliaupdate)
      {
	word = fword->text();
      }
      word.toLower();
      vector<string> tmp;
      tmp.push_back( UnicodeToUTF8(word) );
      addMorph( fword, tmp );
    }
    else if (match == 1) {
      const vector<string> ma = analysis[matches[0]].getMorph();
      addMorph( fword, ma );
    } 
    else {
      if (debugFlag)
	*Log(mbmaLog) << "multiple lemma's" << endl;
      map<string, int> possible_lemmas;
      // find unique lemma's
      for ( size_t q = 0; q < match; ++q ) {
	const vector<string> ma = analysis[matches[q]].getMorph();
	string key;
	for ( size_t p=0; p < ma.size(); ++p )
	  key += ma[p];
	map<string, int>::iterator lemmaIt = possible_lemmas.find( key );
	if (lemmaIt == possible_lemmas.end()){
	  possible_lemmas.insert( make_pair( key,1 ) );
	}
	else {
	  lemmaIt->second++;
	}
      }
      if (debugFlag)
	*Log(mbmaLog) << "#unique lemma's: " << possible_lemmas.size() << endl;
      if ( possible_lemmas.size() < 1) {
	if (debugFlag)
	  *Log(mbmaLog) << "Problem!, no possible lemma's" << endl;
	return;
      }
      // append all unique lemmas
      // find best match
      // loop through all subParts of the tag
      // and match with inflections from each m
      vector<Feature*> feats = pos->select<Feature>();
      if (debugFlag){
	*Log(mbmaLog) << "tag: " << tag << endl;
	for ( size_t q =0 ; q < feats.size(); ++q ) {
	  *Log(mbmaLog) << "\tpart #" << q << ": " << feats[q]->cls() << endl;
	}
      }
      map <string, const vector<string>* > bestMatches;
      int max_count = 0;
      for ( size_t q=0; q < match; ++q ) {
	int match_count = 0;
	string inflection = analysis[matches[q]].getInflection();
	if (debugFlag)
	  *Log(mbmaLog) << "matching " << inflection << " with " << tag << endl;
	for ( size_t u=0; u < feats.size(); ++u ) {
	  map<string,string>::const_iterator conv_tag_p = TAGconv.find(feats[u]->cls());
	  if (conv_tag_p != TAGconv.end()) {
	    string c = conv_tag_p->second;
	    if ( inflection.find( c ) != string::npos )
	      match_count++;
	  }
	}
	if (debugFlag)
	  *Log(mbmaLog) << "score: " << match_count << " max was " << max_count << endl;
	if (match_count >= max_count) {
	  string key;
	  const vector<string> *ma = &analysis[matches[q]].getMorph();
	  for ( size_t p=0; p < ma->size(); ++p )
	    key += (*ma)[p] + "+"; // create uniqe keys
	  
	  if (match_count > max_count) {
	    max_count = match_count;
	    bestMatches.clear();
	  }
	  bestMatches.insert( make_pair(key, ma ) );
	}
      }
      map< string, const vector<string> *>::const_iterator it = bestMatches.begin();
      while ( it != bestMatches.end() ){
	if (debugFlag){
	  string tmp;
	  for ( size_t p=0; p < it->second->size(); ++p )
	    tmp += "[" + (*it->second)[p] + "]";
	  *Log(mbmaLog) << "MBMA add lemma: " << tmp << endl;
	}
	addMorph( fword, *it->second );
	++it;
      }
    }
  }
}  // postprocess

void Mbma::addDeclaration( Document& doc ) const {
  doc.declare( AnnotationType::MORPHOLOGICAL, tagset,
	       "annotator='frog-mbma-" +  version + 
	       "', annotatortype='auto'");
}
  
bool Mbma::Classify( Word* sword ){
  UnicodeString uWord;
  PosAnnotation *pos;
#pragma omp critical(foliaupdate)
  {
    uWord = sword->text();
    pos = sword->annotation<PosAnnotation>();
  }
  string tag = pos->feat("head");
  if ( tag == "SPEC" ){
    string word = UnicodeToUTF8( uWord );
    vector<string> tmp;
    tmp.push_back( word );
    addMorph( sword, tmp );
    return true;
  }
  uWord.toLower();
  if (debugFlag)
    *Log(mbmaLog) << "Classify word: " << uWord << endl;
  
  vector<string> insts = make_instances( uWord );
  vector<string> classes;
  for( size_t i=0; i < insts.size(); ++i ) {
    string ans;
    MTree->Classify( insts[i], ans );
    if ( debugFlag )
      *Log(mbmaLog) << "itt #" << i << ": timbl gave class= " << ans << endl; 
    classes.push_back( ans);
  }

  // fix for 1st char class ==0
  if ( classes[0] == "0" )
    classes[0] = "X";  
  execute( uWord, classes );
  postprocess( sword, pos );
  return true;
}

ostream& operator<< ( ostream& os, const MBMAana& a ){
  os << "tag: " << a.tag << " infl:" << a.infl << " morhemes: " 
     << a.morphemes << " description: " << a.description;
  return os;
}


