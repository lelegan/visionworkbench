/*
 *  GPUProgram.cpp
 *  VWGPU
 *
 *  Created by Ian Saxton on 5/17/06.
 *
 */

#include <fstream>
#include <iostream>

#include <boost/algorithm/string.hpp>

#include <vw/Core/Exception.h>
#include <vw/GPU/GPUProgram.h>
#include <vw/GPU/TokenReplacer.h>


namespace vw { namespace GPU {

int maxErrorLength = 2048;
char errorString[2048];
string GPUShaderDirectory = "shaders/";
string GPUShaderDirectoryCache = "shaders/cache/";

ShaderLanguageChoiceEnum shaderLanguageChoice = SHADER_LANGUAGE_CHOICE_CG_GLSL;

ShaderCompilationStatusEnum shaderCompilationStatus = SHADER_COMPILATION_STATUS_SUCCESS;


#ifdef HAVE_PKG_CG
  bool have_cg = true;
#else
  bool have_cg = false;
#endif


//########################################################################
//#    Base - Program Set
//########################################################################

bool GPUProgramSet::useAssemblyCaching = false;

GPUProgramSet::GPUProgramSet() {

}

GPUProgramSet::~GPUProgramSet() {

}

GPUProgram* GPUProgramSet::get_program(const vector<int>& vertexAttributes, 
				      const vector<int>& fragmentAttributes, 
				      bool verbose)
{
  // LOGGING
  static char  buffer1[256];
  static char buffer2[256];
  buffer1[0] = 0;
  if(vertexAttributes.size()) {
    sprintf(buffer1, "< ");
    for(int i=0; i<vertexAttributes.size(); i++)
      sprintf(buffer1, "%s%i ", buffer1, vertexAttributes[i]);
    sprintf(buffer1, "%s>", buffer1);
  }
  buffer2[0] = 0;
  if(fragmentAttributes.size()) {
    sprintf(buffer2, "< ");
    for(int i=0; i<fragmentAttributes.size(); i++)
      sprintf(buffer2, "%s%i ", buffer2, fragmentAttributes[i]);
    sprintf(buffer2, "%s>", buffer2);
  }
           
  static char buffer3[512];
  GPUProgram* program = NULL;
  sprintf(buffer3, "[GPUProgramSet::GetProgram] VERTEX: %s%s, FRAGMENT: %s%s }    ", vertexBasePath.c_str(), buffer1, fragmentBasePath.c_str(), buffer2);
  WriteToGPULog(buffer3);
// FIND PROGRAM

  WriteToGPULog(GetStringForShaderLanguageChoiceEnum(shaderLanguageChoice));
  WriteToGPULog("  ");
#ifdef HAVE_PKG_CG
  if(shaderLanguageChoice == SHADER_LANGUAGE_CHOICE_CG_GLSL || shaderLanguageChoice == SHADER_LANGUAGE_CHOICE_CG) {
    program = programSet_CG.get_program(vertexAttributes, fragmentAttributes, verbose);
    WriteToGPULog("Trying CG... ");
  }
#endif  

  if(!program && shaderLanguageChoice != SHADER_LANGUAGE_CHOICE_CG) {
    program = programSet_GLSL.get_program(vertexAttributes, fragmentAttributes, verbose);
    WriteToGPULog("Trying GLSL... ");
  }

#ifdef HAVE_PKG_CG
  if(!program && shaderLanguageChoice == SHADER_LANGUAGE_CHOICE_GLSL_CG) {
    program = programSet_CG.get_program(vertexAttributes, fragmentAttributes, verbose);
    WriteToGPULog("Trying CG... ");
  }
#endif  

  if(program) {
    WriteToGPULog("FOUND\n");
  }
  else {
    WriteToGPULog("NOT FOUND!!!\n");
    vw_throw( vw::Exception("[vw::GPU::GPUProgramSet::GetProgram] Program creation failed.") );
  }

  return program;
}



//########################################################################
//#    GLSL - Program Set
//########################################################################

GPUProgramSet_GLSL::GPUProgramSet_GLSL() {

}

GPUProgramSet_GLSL::~GPUProgramSet_GLSL() {
  map<pair<vector<int>, vector<int> >, GPUProgram_GLSL*>::iterator iter_prog;
  for(iter_prog = programMap.begin(); iter_prog != programMap.end(); iter_prog++)
    delete (*iter_prog).second;
}


GPUProgram_GLSL* GPUProgramSet_GLSL::get_program(const vector<int>& vertexAttributes, 
						const vector<int>& fragmentAttributes, 
						bool verbose /* = false */)
 {

	string typeString;
	bool success;
	char charBuffer1[64];
	char charBuffer2[64];

  shaderCompilationStatus = SHADER_COMPILATION_STATUS_SUCCESS;
// Check Program Cache
	map<pair<vector<int>, vector<int> >, GPUProgram_GLSL*>::iterator iter_prog;
	iter_prog = programMap.find(pair<vector<int>, vector<int> >(vertexAttributes, fragmentAttributes));
	if(iter_prog != programMap.end()) {
		return (*iter_prog).second;
	}
	
// Vertex
	GPUVertexShader_GLSL* vertexShader;
		map<vector<int>, GPUVertexShader_GLSL*>::iterator iter_vert;
		iter_vert = vertexMap.find(vertexAttributes);
		if(iter_vert != vertexMap.end()) {
			vertexShader = (*iter_vert).second;
		}
		else {
			vertexShader = new GPUVertexShader_GLSL;
			vertexMap[vertexAttributes] = vertexShader;
			if(vertexBasePath.size()) {
				if(vertexAttributes.size() == 0 || vertexAttributes[0] == 4) 
				  typeString = "_rgba";
				else if(vertexAttributes[0] == 3)
				  typeString = "_rgb";		  
				else	
				  typeString = "_r";
				string vertFilePath = GPUShaderDirectory + vertexBasePath + "_gl_vert" + typeString;
				string vertRawString;
				string vertReplacedString;
				if(!ReadFileAsString(vertFilePath, vertRawString)) {
					if(verbose) printf("Vertex file read error.\n");
					shaderCompilationStatus = SHADER_COMPILATION_STATUS_FILE_ERROR;
					return NULL;
				}
				string& sourceString = vertRawString;
				if(vertexAttributes.size() > 1) {
					TokenReplacer tr;
					string variable;
					for(int i=1; i < vertexAttributes.size(); i++) {
						sprintf(charBuffer1, "%i", i);
						sprintf(charBuffer2, "%i", vertexAttributes[i]);
						tr.AddVariable(charBuffer1, charBuffer2);
					}
					tr.Replace(vertRawString, vertReplacedString);
					sourceString = vertReplacedString;
				}
				if(verbose) { 
				  printf("\n*** %s:\n", vertFilePath.c_str());
				  printf("Specialization: < ");
				  for(int i=0; i < vertexAttributes.size(); i++) {
				    printf("%i ", vertexAttributes[i]);
				  }
				  printf("> \n %s\n", sourceString.c_str());

				}

				if(!vertexShader->compile(sourceString)) {
					shaderCompilationStatus = SHADER_COMPILATION_STATUS_COMPILE_ERROR;
					if(verbose) printf("Vertex compile error.\n");
					return NULL;
				}						
			}
		}
// Fragment
	GPUFragmentShader_GLSL*  fragmentShader;
	map<vector<int>, GPUFragmentShader_GLSL*>::iterator iter_frag;
	iter_frag = fragmentMap.find(fragmentAttributes);
	if(iter_frag != fragmentMap.end()) {
		fragmentShader = (*iter_frag).second;
	}
	else {
		fragmentShader = new GPUFragmentShader_GLSL;
		if(fragmentBasePath.size()) {
			if(fragmentAttributes.size() == 0 || fragmentAttributes[0] == 4) 
			  typeString = "_rgba";
			else if(fragmentAttributes[0] == 3)
			  typeString = "_rgb";		  

			else	
			  typeString = "_r";
			string fragFilePath = GPUShaderDirectory + fragmentBasePath + "_gl_frag" + typeString;
			string fragRawString;
			string fragReplacedString;
			if(!ReadFileAsString(fragFilePath, fragRawString)) {
				if(verbose) printf("Fragment file read error. (%s)\n", fragFilePath.c_str());
				shaderCompilationStatus = SHADER_COMPILATION_STATUS_FILE_ERROR;
				return NULL;
			}
			fragmentShader = new GPUFragmentShader_GLSL;
			string& sourceString = fragRawString;
			if(fragmentAttributes.size() > 1) {
				TokenReplacer tr;
				string variable;
				for(int i=1; i < fragmentAttributes.size(); i++) {
					sprintf(charBuffer1, "%i", i);
					sprintf(charBuffer2, "%i", fragmentAttributes[i]);
					tr.AddVariable(charBuffer1, charBuffer2);
				}
				tr.Replace(fragRawString, fragReplacedString);
				sourceString = fragReplacedString;
			}

			if(verbose) { 
			  printf("\n*** %s:\n", fragFilePath.c_str());
			  printf("Specialization: < ");
			  for(int i=0; i < fragmentAttributes.size(); i++) {
			    printf("%i ", fragmentAttributes[i]);
				  }
			  printf("> \n %s\n", sourceString.c_str());
			  
			}

			if(!fragmentShader->compile(sourceString)) {
			  shaderCompilationStatus = SHADER_COMPILATION_STATUS_COMPILE_ERROR;
			  if(verbose) printf("Fragment compile error.\n");
			  return NULL;
			}
		}						


		fragmentMap[fragmentAttributes] = fragmentShader;

	}
// Program
	GPUProgram_GLSL* program = new GPUProgram_GLSL;
	if(!program->Link(*vertexShader, *fragmentShader)) {
		printf("Program link error.\n");
		return NULL;
	}
	programMap[pair<vector<int>, vector<int> >(vertexAttributes, fragmentAttributes)] = program;
	return program;
}

//########################################################################
//#    GLSL - Program
//########################################################################

bool GPUProgram_GLSL::Link(GPUVertexShader_GLSL& vertex, GPUFragmentShader_GLSL& fragment) {   
	program = glCreateProgramObjectARB();
	if(vertex.is_compiled())
		glAttachObjectARB(program, vertex.get_shader());
	if(fragment.is_compiled())
		glAttachObjectARB(program, fragment.get_shader());
	glLinkProgramARB(program);
	GLsizei errorStringLength;
	GLint isCompiled;
	glGetInfoLogARB(program, maxErrorLength, &errorStringLength, errorString);
	glGetObjectParameterivARB(program, GL_OBJECT_LINK_STATUS_ARB, &isCompiled);
	if(!isCompiled) {
		printf("***PROGRAM LINKER***\n%s\n", errorString);
		program = 0;
		return false;
	}
	return true;
}
 
//########################################################################
//#    GLSL - Vertex Shader
//########################################################################


bool GPUVertexShader_GLSL::compile(string& vertexString) {   
		const char* cStr = vertexString.c_str();
		shader = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
		glShaderSourceARB(shader, 1, (const GLcharARB**) &cStr, NULL);	
		glCompileShaderARB(shader);
		GLsizei errorStringLength;
		GLint isCompiled;
		glGetInfoLogARB(shader, maxErrorLength, &errorStringLength, errorString);
		glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &isCompiled);
		if(!isCompiled) {
      WriteToGPULog("\n*********GLSL Vertex Shader Compilation Error*********\n");
      WriteToGPULog(errorString);
      WriteToGPULog("******************************************************\n");
			return false;
		}
		return true;
}
	
//########################################################################
//#    GLSL - Fragment Shader
//########################################################################



bool GPUFragmentShader_GLSL::compile(string& fragmentString) {   
		const char* cStr = fragmentString.c_str();
		shader = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
		glShaderSourceARB(shader, 1, (const GLcharARB**) &cStr, NULL);	

		glCompileShaderARB(shader);
		GLint isCompiled;
		GLsizei errorStringLength;
		glGetInfoLogARB(shader, maxErrorLength, &errorStringLength, errorString);
		glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &isCompiled);
		if(!isCompiled) { 
      WriteToGPULog("\n*********GLSL Fragment Shader Compilation Error*********\n");
      WriteToGPULog(errorString);
      WriteToGPULog("********************************************************\n");
			return false;
		}
		return true;
}



//########################################################################
//#    CG Specific Code
//########################################################################

#ifdef HAVE_PKG_CG

CGerror CGCheckError();

//########################################################################
//#    GPUShader_CG
//########################################################################


CGcontext cgContext = NULL;
void InitCGContext();
void CGErrorCallback();


GPUShader_CG::GPUShader_CG() {
  program = NULL;
}

GPUShader_CG::~GPUShader_CG() {
  if(program)
    cgDestroyProgram(program);
}

bool GPUShader_CG::compile_source_with_string(const char *sourceString, CGprofile profile, const char *entry, const char **args) {
  InitCGContext();
  // Delete old program object
  if(program)
    cgDestroyProgram(program);
  // Compile
  GPUShader_CG::profile = profile;
  program = cgCreateProgram(cgContext, CG_SOURCE, sourceString, profile, entry, args);
  // Check for errors
  CGerror error = cgGetError();
  if(!cgIsProgramCompiled(program)) {
    program = NULL;
    return false;
  }
  // Load into cgGL
  cgGLLoadProgram(program);
  return true;
}


bool GPUShader_CG::compile_source_with_file(const char *sourceString, CGprofile profile, const char *entry, const char **args) {
  InitCGContext();
  // Delete old program object
  if(program)
    cgDestroyProgram(program);
  // Compile
  program = cgCreateProgramFromFile(cgContext, CG_SOURCE, sourceString, profile, entry, args);
  GPUShader_CG::profile = profile;
  // printf("%s", cgGetLastListing(cgContext));
  // Check for errors
  CGerror error = cgGetError();
  if(!cgIsProgramCompiled(program)) {
    printf("*** ERROR (GPUShader_CG::compile_source_with_string()  Compile Failed. Exiting...\n");
    program = NULL;
    exit(0);
  }
  // Load into cgGL
  cgGLLoadProgram(program);
  return true;
}

bool GPUShader_CG::save_compiled_file(const char *file) {
  ofstream outFile(file);
  if(!outFile) {
    return false;
  }
  if(!is_compiled()) {
    return false;
  }
  const char *entryString = cgGetProgramString(program, CG_PROGRAM_ENTRY);
  outFile << entryString << " ";
  const char *profileString = cgGetProgramString(program, CG_PROGRAM_PROFILE);
  outFile << profileString << " ";
  const char *objectString = cgGetProgramString(program, CG_COMPILED_PROGRAM);
  outFile << objectString;

  outFile.close();
  return true;
}

bool GPUShader_CG::load_compiled_file(const char *file) {
  InitCGContext();
  // Input File
  ifstream inFile(file);
  if(!inFile) {
    return false;
  }
  // Read Entry and Profile Strings
  string entryString;
  inFile >> entryString;
  string profileString;
  inFile >> profileString;

  CGprofile profile = cgGetProfile(profileString.c_str());
  if(profile == CG_PROFILE_UNKNOWN) {
    return false;
  }
  // Read Object String
  int objectStringStart = ((int) inFile.tellg()) + 1;
  inFile.seekg(0, ios::end);
  int objectStringLength = ((int) inFile.tellg()) - objectStringStart;
  inFile.seekg(objectStringStart, ios::beg);
  char *objectString = (char*) malloc(objectStringLength + 1);
  inFile.read(objectString, objectStringLength);
  inFile.close();
  objectString[objectStringLength] = 0;
  // Delete old program object
  if(program)
    cgDestroyProgram(program);
  CGCheckError();
  program = cgCreateProgram(cgContext, CG_OBJECT, objectString, profile, entryString.c_str(), NULL);
  GPUShader_CG::profile = profile;
  // Check for errors
  if(!is_compiled() || CGCheckError()) {
    program = NULL;
    return false;
  }
  CGCheckError();
  cgGLLoadProgram(program);
  if(CGCheckError()) {
    return false;
  }
  return true;
}




void GPUShader_CG::install() {
  //cgGLEnableProfile(CG_PROFILE_FP30);
  cgGLEnableProfile(profile);
  cgGLBindProgram(program);
}


void GPUShader_CG::uninstall() {
  cgGLDisableProfile(profile);
}

//########################################################################
//#    GPUProgramSet_CG
//########################################################################


GPUProgramSet_CG::GPUProgramSet_CG() {

}

GPUProgramSet_CG::~GPUProgramSet_CG() {
  map<pair<vector<int>, vector<int> >, GPUProgram_CG*>::iterator iter_prog;
  for(iter_prog = programMap.begin(); iter_prog != programMap.end(); iter_prog++)
    delete (*iter_prog).second;
}


  GPUProgram_CG* GPUProgramSet_CG::get_program(const vector<int>& vertexAttributes, 
					      const vector<int>& fragmentAttributes, 
					      bool verbose)
  {
    bool useAssemblyCaching = GPUProgramSet::get_use_assembly_caching();
    char charBuffer1[256];
    char charBuffer2[32];
    shaderCompilationStatus = SHADER_COMPILATION_STATUS_SUCCESS;
 // Check cache for program
    map<pair<vector<int>, vector<int> >, GPUProgram_CG*>::iterator iter_prog;
    iter_prog = programMap.find(pair<vector<int>, vector<int> >(vertexAttributes, fragmentAttributes));
    if(iter_prog != programMap.end()) {
      return (*iter_prog).second;
    }
    if(verbose) printf("[GPUProgramSet_CG::GetProgram] Not Found in Cache.\n");
    //
    auto_ptr<GPUShader_CG> vertexShader(NULL);
    auto_ptr<GPUShader_CG> fragmentShader(NULL);									    
 // Get Source Strings
    string vertRawString;
    string fragRawString;
    string fragAssemblyFilePath;
    bool fragComplete = false;
    // VERTEX
    if(!vertexBasePath.empty()) {
      string typeString;
      if(vertexAttributes.size() == 0 || vertexAttributes[0] > 1) 
	typeString = "_rgba";
      else	
	typeString = "_r";
      string vertFilePath = GPUShaderDirectory + vertexBasePath + "_cg_vert" + typeString;
      
      if(!ReadFileAsString(vertFilePath, vertRawString)) {
	if(verbose) printf("[GPUProgramSet_CG::GetProgram] Error: Vertex File Not Found.\n");
	shaderCompilationStatus = SHADER_COMPILATION_STATUS_FILE_ERROR;
	return NULL;
      }
    }
    // FRAGMENT
     if(!fragmentBasePath.empty()) {
       // Create new shader
      fragmentShader.reset(new GPUShader_CG);
      // Make Base Path
      string typeString;
      if(fragmentAttributes.size() == 0 || fragmentAttributes[0] > 1) 
	typeString = "_rgba";
      else     
	typeString = "_r";
      string fragFilePath = GPUShaderDirectory + fragmentBasePath + "_cg_frag" + typeString;
      // If using assembly caching, create assembly path and attempt to load it.
      if(useAssemblyCaching) {
	string modifiedFragmentBasePath = fragmentBasePath;
	boost::algorithm::replace_all(modifiedFragmentBasePath, "/", "_");
	fragAssemblyFilePath = GPUShaderDirectoryCache + modifiedFragmentBasePath + "_cg_frag" + typeString;
	for(int i=1; i < fragmentAttributes.size(); i++) {
	  sprintf(charBuffer1, "_%i", fragmentAttributes[i]);
	  fragAssemblyFilePath += charBuffer1;
	}			     
	fragAssemblyFilePath += ".cache";

	if(fragmentShader->load_compiled_file(fragAssemblyFilePath.c_str())) {
	  fragComplete = true;
	  if(verbose) printf("[GPUProgramSet_CG::GetProgram] Assembly Fragment file compiled.\n");
	}
	else {
	  if(verbose) printf("[GPUProgramSet_CG::GetProgram] Assembly Fragment file not compiled.\n");
	}
      }
      // If necessary, read source string		      
      if(!fragComplete) {
	if(!ReadFileAsString(fragFilePath, fragRawString)) {
	  if(verbose) printf("[GPUProgramSet_CG::GetProgram] Error: Fragment File Not Found.\n");
	  shaderCompilationStatus = SHADER_COMPILATION_STATUS_FILE_ERROR;
	  return NULL;
	}
	if(verbose) printf("[GPUProgramSet_CG::GetProgram] Fragment source file read.\n");
      }
    }
// Specialize Strings and Compile Shaders
    // VERTEX
    if(!vertexBasePath.empty()) {
      string vertReplacedString;      
      string& sourceString = vertRawString;
      if(vertexAttributes.size() > 1) {     // Specialize String
	TokenReplacer tr;
	string variable;
	for(int i=1; i < vertexAttributes.size(); i++) {
	  sprintf(charBuffer1, "%i", i);
	  sprintf(charBuffer2, "%i", vertexAttributes[i]);
	  tr.AddVariable(charBuffer1, charBuffer2);
	}
	tr.Replace(vertRawString, vertReplacedString);
	sourceString = vertReplacedString;
      }
      if(verbose) { 
	printf("Specialization: < ");
	for(int i=0; i < vertexAttributes.size(); i++) {
	  printf("%i ", vertexAttributes[i]);
	}
	printf("> \n %s\n", sourceString.c_str());
      }
      if(!vertexShader->compile_source_with_string(sourceString.c_str(), CG_PROFILE_VP30, "main", NULL)) {
	if(verbose) printf("[GPUProgramSet_CG::GetProgram] Vertex Shader Compilation Failed.\n");
	shaderCompilationStatus = SHADER_COMPILATION_STATUS_COMPILE_ERROR;
	return NULL;	
      }
    }
    if(verbose) printf("[GPUProgramSet_CG::GetProgram] Vertex Shader Done.\n");
    // FRAGMENT
    if(!fragmentBasePath.empty() && !fragComplete) {
      string fragReplacedString;      
      string& sourceString = fragRawString;
      if(fragmentAttributes.size() > 1) {     // Specialize String
	TokenReplacer tr;
	string variable;
	for(int i=1; i < fragmentAttributes.size(); i++) {
	  sprintf(charBuffer1, "%i", i);
	  sprintf(charBuffer2, "%i", fragmentAttributes[i]);
	  tr.AddVariable(charBuffer1, charBuffer2);
	}
	tr.Replace(fragRawString, fragReplacedString);
	sourceString = fragReplacedString;
      }
      if(verbose) { 
	printf("Specialization: < ");
	for(int i=0; i < fragmentAttributes.size(); i++) {
	  printf("%i ", fragmentAttributes[i]);
	}
	printf("> \n %s\n", sourceString.c_str());
      }
      if(!fragmentShader->compile_source_with_string(sourceString.c_str(), CG_PROFILE_FP30, "main", NULL)) {
	if(verbose) printf("[GPUProgramSet_CG::GetProgram] Fragment Shader Compilation Failed.\n");
	shaderCompilationStatus = SHADER_COMPILATION_STATUS_COMPILE_ERROR;
	return NULL;	
      }
      if(useAssemblyCaching) {
	if(fragmentShader->save_compiled_file(fragAssemblyFilePath.c_str())) {
	  if(verbose) printf("[GPUProgramSet_CG::GetProgram] Assembly Fragment file saved.\n");
	}
      }
    }
    if(verbose) printf("[GPUProgramSet_CG::GetProgram] Fragment Shader Done.\n");
  // PROGRAM - Make new program, put in cache and return it 
    if(verbose) printf("[GPUProgramSet_CG::GetProgram] Shader compilation succeeded.\n");
    GPUProgram_CG* program = new GPUProgram_CG(vertexShader.get(), fragmentShader.get());
    vertexShader.release();
    fragmentShader.release();

    programMap[pair<vector<int>, vector<int> >(vertexAttributes, fragmentAttributes)] = program;
    return program;
  }


//########################################################################
//#                           CG - Internal Globals                      #
//########################################################################


CGerror CGCurrentError = (CGerror) 0;

void InitCGContext() {
  if(!cgContext) {
    //cgSetErrorCallback(&CGErrorCallback);
    cgContext = cgCreateContext();
  }
}


void CGErrorCallback() {
    while(1) {
      CGerror error = cgGetError();
      if(error == 0)
	break;
      else {
	CGCurrentError = error;
	printf("***CG ERROR:   %s \n", cgGetErrorString(error));
      }
    }
}

  CGerror CGCheckError() {
    CGerror temp = CGCurrentError;
    CGCurrentError = (CGerror) 0;
    return temp;
  }



#endif // ifdef HAVE_PKG_CG___


} } // namespaces GPU, vw


