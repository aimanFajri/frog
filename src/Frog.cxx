/* ex: set tabstop=8 expandtab: */
/*
  Copyright (c) 2006 - 2017
  CLST  - Radboud University
  ILK   - Tilburg University

  This file is part of frog:

  A Tagger-Lemmatizer-Morphological-Analyzer-Dependency-Parser for
  several languages

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
      https://github.com/LanguageMachines/frog/issues
  or send mail to:
      lamasoftware (at ) science.ru.nl

*/

#include <cstdlib>
#include <cstdio>
#include <sys/wait.h>
#include <signal.h>
#include <string>
#include <cstring>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include "config.h"
#ifdef HAVE_OPENMP
#include <omp.h>
#endif

#include "timbl/TimblAPI.h"
#include "ticcutils/FdStream.h"
#include "ticcutils/ServerBase.h"

// individual module headers

#include "frog/Frog.h" //internal interface, included by all modules
#include "frog/FrogAPI.h" //public API interface
#include "frog/ucto_tokenizer_mod.h"
#include "frog/pos_tagger_mod.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"

using namespace std;
using namespace folia;
using namespace TiCC;

#define LOG *Log(theErrLog)

string testDirName;
string outputFileName;
bool wantOUT;
string XMLoutFileName;
string outputDirName;
string xmlDirName;
set<string> fileNames;

/* assumptions:
   each components gets its own configfile per cmdline options
   another possibility is the U: en K: style option seperation

   each component has a read_settings_file in own namespace,
        which sets variables global to that namespace
	to be used in the init() for that namespace

   Further,
   each component provides a Test(File) which writes output to
        File.<componentname>_out
   and a Classify(Instance) which produces relevant output
        as a string return
	or somehwere else,
   to be determined later,
   after pre- and postprocessing raw classification data

*/

Configuration configuration;

void usage( ) {
  cout << endl << "Options:\n";
  cout << "\t============= INPUT MODE (mandatory, choose one) ========================\n"
       << "\t -e <encoding>          specify encoding of the input (default UTF-8)\n"
       << "\t -t <testfile>          Run frog on this file\n"
       << "\t -x <testfile>          Run frog on this FoLiA XML file. Or the files from 'testdir'\n"
       << "\t --textclass=<cls>      use the specified class to search for text in the the FoLia docs. (default 'current').\n"
       << "\t\t\t the same value is used for output too.\n"
       << "\t --inputclass=<cls>     use the specified class to search for text in the the FoLia docs. (default 'current') \n"
       << "\t --outputclass=<cls>    use the specified class to output text in the the FoLia docs. (default 'inputclass') \n"
       << "\t --testdir=<directory>  All files in this dir will be tested\n"
       << "\t --uttmarker=<mark>     utterances are separated by 'mark' symbols"
       << "\t                        (default none)\n"
       << "\t -n                     Assume input file to hold one sentence per line\n"
       << "\t --max-parser-tokens=<n> inhibit parsing when a sentence contains over 'n' tokens. (default: 500, needs already 16Gb of memory!)\n"
       << "\t -Q                   Enable quote detection in tokeniser.\n"
       << "\t============= MODULE SELECTION ==========================================\n"
       << "\t --skip=[mptncla]    Skip Tokenizer (t), Lemmatizer (l), Morphological Analyzer (a), Chunker (c), Multi-Word Units (m), Named Entity Recognition (n), or Parser (p) \n"
       << "\t============= CONFIGURATION OPTIONS =====================================\n"
       << "\t -c <filename>    Set configuration file (default "
       << FrogAPI::defaultConfigFile() << ")\n"
       << "\t --language <language-list>  Set the languages. e.g. --language=nld,eng,por"
       << "\t\t The first language in the list will be the default. (default dutch).\n"
       << "\t============= OUTPUT OPTIONS ============================================\n"
       << "\t -o <outputfile>	    Output columned output to file, instead of default stdout\n"
       << "\t -X <xmlfile>          Output also to an XML file in FoLiA format\n"
       << "\t --id=<docid>          Document ID, used in FoLiA output. (Default 'untitled')\n"
       << "\t --outputdir=<dir>     Output to dir, instead of default stdout\n"
       << "\t --xmldir=<dir>        Use 'dir' to output FoliA XML to.\n"
       << "\t --deep-morph          add deep morphological information to the output\n"
       << "\t============= OTHER OPTIONS ============================================\n"
       << "\t -h or --help          give some help.\n"
       << "\t -V or --version       Show version info.\n"
       << "\t -d <debug level>  (for more verbosity)\n"
       << "\t --debug=<module><level>,<module><level>... (eg --debug=l5,n3) \n"
       << "\t\t Set debug value for Tokenizer (t), Lemmatizer (l), Morphological Analyzer (a), Chunker (c), Multi-Word Units (m), Named Entity Recognition (n), or Parser (p) \n"
       << "\t -S <port>              Run as server instead of reading from testfile\n"
#ifdef HAVE_OPENMP
       << "\t --threads=<n>       Use a maximum of 'n' threads. Default: 8. \n"
#endif
       << "\t                     (but always 1 for server mode)\n";
}

bool parse_args( TiCC::CL_Options& Opts,
		 FrogOptions& options,
		 LogStream* theErrLog ) {
  // process the command line and fill FrogOptions to initialize the API
  // also fill some globals we use for our own main.

  // is a language-list specified? Default is dutch
  string language;
  string languages;
  Opts.extract ("language", languages );
  string configFileName;
  if ( languages.empty() ){
    // ok no languages parameter.
    // us a (default) configfile. Dutch
    configFileName = FrogAPI::defaultConfigFile("nld");
    language = "none";
  }
  else {
    vector<string> lang;
    int num = split_at( languages, lang, "," );
    if ( num < 0 ){
      cerr<< "invalid value in --languages=" << languages
	  << " option. " << endl;
      return false;
    }
    language = lang[0]; // the first mentioned is the default.
    if ( num > 1 ){
      cerr << "WARNING: you used the --language=" << languages << " option"
	   << " with more then one language " << endl
	   << "\t specified. These values will be handled to the tokenizer,"
	   << " but Frog"<< endl
	   << "\t will only handle the first language: " << language
	   << " for further processing!" << endl;
    }
    configFileName = FrogAPI::defaultConfigFile(language);
    if ( !TiCC::isFile( configFileName ) ){
      cerr << "configuration file: " << configFileName << " not found" << endl;
      cerr << "Did you correctly install the frogdata package for language="
	   << language << "?" << endl;
      configFileName = FrogAPI::defaultConfigFile();
      cerr << "using fallback configuration file: " << configFileName << endl;
    }
  }
  options.language = language;
  // override default config settings when a configfile is specified
  Opts.extract( 'c',  configFileName );
  if ( configuration.fill( configFileName ) ){
    LOG << "config read from: " << configFileName << endl;
    string vers = configuration.lookUp( "version" );
    if ( !vers.empty() ){
      LOG << "configuration version = " << vers << endl;
    }
  }
  else {
    cerr << "failed to read configuration from '" << configFileName << "' !!" << endl;
    cerr << "Did you correctly install the frogdata package for language="
	 << language << "?" << endl;
    return false;
  }
  if ( !languages.empty() ){
    configuration.setatt( "languages", languages, "tokenizer" );
  }
  string value;
  // debug opts
  if ( Opts.extract ('d', value) ) {
    if ( !stringTo<int>( value, options.debugFlag ) ){
      LOG << "-d value should be an integer" << endl;
      return false;
    }
    configuration.setatt( "debug", value );
  }
  else {
    configuration.setatt( "debug", "0" );
  }
  if ( Opts.extract( "debug", value ) ) {
    value = TiCC::lowercase( value );
    vector<string> vec;
    TiCC::split_at( value, vec, "," );
    for ( const auto& val : vec ){
      char mod = val[0];
      string value = val.substr(1);
      int dbval = 0;
      if ( !stringTo<int>( value, dbval ) ){
	cerr << "expected integer value for --debug=" << mod << value << endl;
	return false;
      }
      switch ( mod ){
      case 't':
	configuration.setatt( "debug", value, "tokenizer" );
	break;
      case 'l':
	configuration.setatt( "debug", value, "mblem" );
	break;
      case 'a':
	configuration.setatt( "debug", value, "mbma" );
	break;
      case 'm':
	configuration.setatt( "debug", value, "mwu" );
	break;
      case 'c':
	configuration.setatt( "debug", value, "IOB" );
	break;
      case 'n':
	configuration.setatt( "debug", value, "NER" );
	break;
      case 'p':
	configuration.setatt( "debug", value, "parser" );
	break;
      default:
	cerr << "unknown module '" << mod << "'" << endl;
	return false;
      }
    }
  }

  options.doSentencePerLine = Opts.extract( 'n' );
  options.doQuoteDetection = Opts.extract( 'Q' );
  if ( Opts.extract( "skip", value )) {
    string skip = value;
    if ( skip.find_first_of("tT") != string::npos )
      options.doTok = false;
    if ( skip.find_first_of("lL") != string::npos )
      options.doLemma = false;
    if ( skip.find_first_of("aA") != string::npos )
      options.doMorph = false;
    if ( skip.find_first_of("mM") != string::npos )
      options.doMwu = false;
    if ( skip.find_first_of("cC") != string::npos )
      options.doIOB = false;
    if ( skip.find_first_of("nN") != string::npos )
      options.doNER = false;
    if ( skip.find_first_of("pP") != string::npos )
      options.doParse = false;
    else if ( !options.doMwu ){
      LOG << " Parser disabled, because MWU is deselected" << endl;
      options.doParse = false;
    }
    Opts.remove("skip");
  };

  if ( Opts.extract( "deep-morph" ) ) {
    options.doDeepMorph = true;
    options.doMorph = true;
  }

  Opts.extract( 'e', options.encoding );

  if ( Opts.extract( "max-parser-tokens", value ) ){
    if ( !stringTo<unsigned int>( value, options.maxParserTokens ) ){
      LOG << "max-parser-tokens value should be an integer" << endl;
      return false;
    }
  }

  options.doServer = Opts.extract('S', options.listenport );

#ifdef HAVE_OPENMP
  if ( options.doServer ) {
    // run in one thread in server mode, forking is too expensive for lots of small snippets
    options.numThreads =  1;
    Opts.extract( "threads", value ); //discard threads option
  }
  else if ( Opts.extract( "threads", value ) ){
    int num;
    if ( !stringTo<int>( value, num ) || num < 1 ){
      LOG << "threads value should be a positive integer" << endl;
      return false;
    }
    options.numThreads = num;
  }
#else
  if ( Opts.extract( "threads", value ) ){
    LOG << "WARNING!\n---> There is NO OpenMP support enabled\n"
		    << "---> --threads=" << value << " is ignored.\n"
		    << "---> Will continue on just 1 thread." << endl;
  }
#endif

  if ( Opts.extract( "keep-parser-files" ) ){
    LOG << "keep-parser-files option not longer supported. (ignored)" << endl;
  }
  if ( Opts.extract( "tmpdir" ) ){
    LOG << "tmpdir option not longer supported. (ignored)" << endl;
  }
  string TestFileName;
  if ( Opts.extract( "testdir", TestFileName ) ) {
    options.doDirTest = true;
    testDirName = TestFileName;
    if ( testDirName.back() != '/' ){
      testDirName += "/";
    }
    if ( !TiCC::isDir( testDirName ) ){
      LOG << "input dir " << testDirName << " not readable" << endl;
      return false;
    }
  }
  else if ( Opts.extract( 't', TestFileName ) ) {
    if ( !TiCC::isFile( TestFileName ) ){
      LOG << "input stream " << TestFileName << " is not readable" << endl;
      return false;
    }
  };
  wantOUT = false;
  if ( Opts.extract( "outputdir", outputDirName )) {
    if ( outputDirName.back() != '/' ){
      outputDirName += "/";
    }
    if ( !TiCC::createPath( outputDirName ) ){
      LOG << "output dir " << outputDirName << " not readable" << endl;
      return false;
    }
    wantOUT = true;
  }
  else if ( Opts.extract( 'o', outputFileName ) ){
    wantOUT = true;
  };
  options.doXMLout = false;
  Opts.extract( "id", options.docid );
  if ( Opts.extract( "xmldir", xmlDirName ) ){
    if ( xmlDirName.back() != '/' ){
      xmlDirName += "/";
    }
    if ( !TiCC::createPath( xmlDirName ) ){
      LOG << "XML output dir " << xmlDirName << " not readable" << endl;
      return false;
    }
    options.doXMLout = true;
  }
  else if ( Opts.extract('X', XMLoutFileName ) ){
    options.doXMLout = true;
  }

  options.doKanon = Opts.extract("KANON");

  options.doXMLin = false;
  if ( Opts.extract ('x', value ) ){
    options.doXMLin = true;
    if ( !value.empty() ){
      if ( !xmlDirName.empty() || !testDirName.empty() ){
	LOG << "-x may not provide a value when --testdir or --xmldir is provided" << endl;
	return false;
      }
      TestFileName = value;
      if ( !TiCC::isFile( TestFileName ) ){
	LOG << "input stream " << value << " is not readable" << endl;
	return false;
      }
    }
  }
  string textclass;
  string inputclass;
  string outputclass;
  Opts.extract( "textclass", textclass );
  Opts.extract( "inputclass", inputclass );
  Opts.extract( "outputclass", outputclass );
  if ( !options.doXMLin ){
    if ( !textclass.empty() ){
      LOG << "--textclass is only valid when -x is also present" << endl;
      return false;
    }
    if ( !inputclass.empty() ){
      LOG << "--inputclass is only valid when -x is also present" << endl;
      return false;
    }
    if ( !outputclass.empty() ){
      LOG << "--outputclass is only valid when -x is also present" << endl;
      return false;
    }
  }
  else { // FoLiA files...
    if ( !textclass.empty() ){
      if ( !inputclass.empty() || !outputclass.empty() ){
	LOG << "when --textclass is specified, --inputclass or --outputclass may NOT be present." << endl;
	return false;
      }
      options.inputclass = textclass;
      options.outputclass = textclass;
      configuration.setatt( "inputclass", textclass );
      configuration.setatt( "outputclass", textclass );
    }
    else {
      if ( !inputclass.empty() ){
	options.inputclass = inputclass;
	configuration.setatt( "inputclass", inputclass );
	if ( outputclass.empty() ){
	  options.outputclass = inputclass;
	  configuration.setatt( "outputclass", inputclass );
	}
      }
      if ( !outputclass.empty() ){
	options.outputclass = outputclass;
	configuration.setatt( "outputclass", outputclass );
      }
    }
  }
  if ( !XMLoutFileName.empty() && !testDirName.empty() ){
    LOG << "useless -X value" << endl;
    return false;
  }

  Opts.extract ("uttmarker", options.uttmark );
  if ( !testDirName.empty() ){
    if ( options.doXMLin )
      getFileNames( testDirName, ".xml", fileNames );
    else
      getFileNames( testDirName, "", fileNames );
    if ( fileNames.empty() ){
      LOG << "error: couldn't find any files in directory: "
		      << testDirName << endl;
      return false;
    }
  }
  if ( fileNames.empty() ){
    if ( !TestFileName.empty() ){
      fileNames.insert( TestFileName );
    }
    else {
      vector<string> mass = Opts.getMassOpts();
      for ( const auto& name : mass ){
	fileNames.insert( name );
      }
    }
  }
  if ( fileNames.size() > 1 ){
    if ( !XMLoutFileName.empty() ){
      LOG << "'-X " << XMLoutFileName
		      << "' is invalid for multiple inputfiles." << endl;
      return false;
    }
  }
  if ( !Opts.empty() ){
    LOG << "unhandled commandline options: " << Opts.toString() << endl;

    return false;
  }
  return true;
}

bool StillRunning = true;

void KillServerFun( int Signal ){
  if ( Signal == SIGTERM ){
    cerr << "KillServerFun caught a signal SIGTERM" << endl;
    sleep(5); // give children some spare time...
    StillRunning = false;
  }
}


int main(int argc, char *argv[]) {
  cerr << "frog " << VERSION << " (c) CLTS, ILK 1998 - 2017" << endl
       << "CLST  - Centre for Language and Speech Technology,"
       << "Radboud University" << endl
       << "ILK   - Induction of Linguistic Knowledge Research Group,"
       << "Tilburg University" << endl;
  cerr << "based on [" << Tokenizer::VersionName() << ", "
       << folia::VersionName() << ", "
       << Timbl::VersionName() << ", "
       << TimblServer::VersionName() << ", "
       << Tagger::VersionName() << "]" << endl;
  LogStream *theErrLog = new LogStream( cerr, "frog-", StampMessage );
  std::ios_base::sync_with_stdio(false);
  FrogOptions options;

  try {
    TiCC::CL_Options Opts("c:e:o:t:x::X::nQhVd:S:",
			  "textclass:,inputclass:,outputclass:,testdir:,"
			  "uttmarker:,max-parser-tokens:,"
			  "skip:,id:,outputdir:,xmldir:,tmpdir:,deep-morph,"
			  "help,language:,"
			  "debug:,keep-parser-files,version,threads:,KANON");
    Opts.init(argc, argv);
    if ( Opts.is_present('V' ) || Opts.is_present("version" ) ){
      // we already did show what we wanted.
      return EXIT_SUCCESS;
    }
    if ( Opts.is_present( 'h' )
	 || Opts.is_present( "help" ) ) {
      usage();
      return EXIT_SUCCESS;
    };
    bool parsed = parse_args( Opts, options, theErrLog );
    if (!parsed) {
      throw runtime_error( "init failed" );
    }
    FrogAPI frog( options, configuration, theErrLog );

    if ( !fileNames.empty() ) {
      string outPath = outputDirName;
      string xmlPath = xmlDirName;

      ostream *outS = 0;
      if ( !outputFileName.empty() ){
	if ( !TiCC::createPath( outputFileName ) ) {
	  LOG << "problem: unable to create outputfile: "
			  << outputFileName << endl;
	  return EXIT_FAILURE;
	}
        outS = new ofstream( outputFileName );
      }
      if ( fileNames.size() > 1 ){
	LOG << "start procesessing " << fileNames.size() << " files..." << endl;
      }
      for ( auto const& name : fileNames ){
	string testName = testDirName + name;
	if ( !TiCC::isFile( testName ) ){
	  LOG << "skip " << testName << " (file not found )"
			  << endl;
	  continue;
	}
	string outName;
	if ( outS == 0 ){
	  if ( wantOUT ){
	    if ( options.doXMLin ){
	      if ( !outPath.empty() )
		outName = outPath + name + ".out";
	    }
	    else {
	      outName = outPath + name + ".out";
	    }
	    if ( !TiCC::createPath( outName ) ) {
	      LOG << "problem frogging: " << name << endl
			      << "unable to create outputfile: " << outName
			      << endl;
	      continue;
	    }
	    outS = new ofstream( outName );
	  } else {
	    outS = &cout;
	  }
	}
	string xmlOutName = XMLoutFileName;
	if ( xmlOutName.empty() ){
	  if ( !xmlDirName.empty() ){
	    if ( name.rfind(".xml") == string::npos )
	      xmlOutName = xmlPath + name + ".xml";
	    else
	      xmlOutName = xmlPath + name;
	  }
	  else if ( options.doXMLout )
	    xmlOutName = name + ".xml"; // do not clobber the inputdir!
	}
	if ( !xmlOutName.empty() && !TiCC::createPath( xmlOutName ) ){
	  LOG << "problem frogging: " << name << endl
			  << "unable to create outputfile: " << xmlOutName
			  << endl;
	  continue;
	}
	LOG << "Frogging " << testName << endl;
	try {
	  frog.FrogFile( testName, *outS, xmlOutName );
	}
	catch ( exception& e ){
	  LOG << "problem frogging: " << name << endl
			  << e.what() << endl;
	  continue;
	}
	if ( !outName.empty() ){
	  LOG << "results stored in " << outName << endl;
	  delete outS;
	  outS = 0;
	}
      }
      if ( !outputFileName.empty() ){
	LOG << "results stored in " << outputFileName << endl;
	delete outS;
      }
    }
    else if ( options.doServer ) {
      //first set up some things to deal with zombies
      struct sigaction action;
      action.sa_handler = SIG_IGN;
      sigemptyset(&action.sa_mask);
#ifdef SA_NOCLDWAIT
      action.sa_flags = SA_NOCLDWAIT;
#endif
      sigaction(SIGCHLD, &action, NULL);
      struct sigaction act;
      sigaction( SIGTERM, NULL, &act ); // get current action
      act.sa_handler = KillServerFun;
      act.sa_flags &= ~SA_RESTART;      // do not continue after SIGTERM
      sigaction( SIGTERM, &act, NULL );

      srand((unsigned)time(0));

      LOG << "Listening on port " << options.listenport << "\n";

      try {
	// Create the socket
	Sockets::ServerSocket server;
	if ( !server.connect( options.listenport ) )
	  throw( runtime_error( "starting server on port " + options.listenport + " failed" ) );
	if ( !server.listen( 5 ) ) {
	  // maximum of 5 pending requests
	  throw( runtime_error( "listen(5) failed" ) );
	}
	while ( StillRunning ) {
	  Sockets::ServerSocket conn;
	  if ( server.accept( conn ) ){
	    LOG << "New connection..." << endl;
	    int pid = fork();
	    if (pid < 0) {
	      string err = strerror(errno);
	      LOG << "ERROR on fork: " << err << endl;
	      throw runtime_error( "FORK failed: " + err );
	    }
	    else if (pid == 0)  {
	      frog.FrogServer( conn );
	      return EXIT_SUCCESS;
	    }
	  }
	  else {
	    throw( runtime_error( "Accept failed" ) );
	  }
	}
      }
      catch ( std::exception& e ) {
	LOG << "Server error:" << e.what() << " Exiting." << endl;
	throw;
      }
    }
    else {
      // interactive mode
      frog.FrogInteractive();
    }
  }
  catch ( const exception& e ){
    LOG << "fatal error: " << e.what() << endl;
    return EXIT_FAILURE;
  }
  delete theErrLog;
  return EXIT_SUCCESS;
}
