#include "StdAfx.h"
// LuaParser.cpp: implementation of the LuaParser class.
//
//////////////////////////////////////////////////////////////////////

#include "LuaParser.h"

#include <algorithm>
#include <boost/regex.hpp>
#include <SDL/SDL_timer.h>

#include "LuaInclude.h"

#include "LuaUtils.h"

#include "System/LogOutput.h"
#include "System/FileSystem/FileHandler.h"
#include "System/FileSystem/VFSHandler.h"
#include "System/Platform/FileSystem.h"
#include "System/Platform/errorhandler.h"


#if (LUA_VERSION_NUM < 500)
#  define LUA_OPEN_LIB(L, lib) lib(L)
#else
#  define LUA_OPEN_LIB(L, lib) \
     lua_pushcfunction((L), lib); \
     lua_pcall((L), 0, 0, 0); 
#endif


/******************************************************************************/
/******************************************************************************/

LuaParser* LuaParser::currentParser = NULL;


/******************************************************************************/
/******************************************************************************/

static void SetupStdLibs(lua_State* L)
{
	LUA_OPEN_LIB(L, luaopen_base);
	LUA_OPEN_LIB(L, luaopen_math);
	LUA_OPEN_LIB(L, luaopen_table);
	LUA_OPEN_LIB(L, luaopen_string);
	//LUA_OPEN_LIB(L, luaopen_io);
	//LUA_OPEN_LIB(L, luaopen_os);
	//LUA_OPEN_LIB(L, luaopen_package);
	//LUA_OPEN_LIB(L, luaopen_debug);

	// delete some dangerous/unsynced functions
	lua_pushnil(L); lua_setglobal(L, "dofile");
	lua_pushnil(L); lua_setglobal(L, "loadfile");
	lua_pushnil(L); lua_setglobal(L, "loadlib");
	lua_pushnil(L); lua_setglobal(L, "require");
	lua_pushnil(L); lua_setglobal(L, "gcinfo");
	lua_pushnil(L); lua_setglobal(L, "collectgarbage");

	// FIXME: replace "random" as in LuaHandleSynced (can write your own for now)
	lua_getglobal(L, "math");
	lua_pushstring(L, "random");     lua_pushnil(L); lua_rawset(L, -3);
	lua_pushstring(L, "randomseed"); lua_pushnil(L); lua_rawset(L, -3);
	lua_pop(L, 1); // pop "math"
}


/******************************************************************************/
/******************************************************************************/
//
//  LuaParser
//

LuaParser::LuaParser(const string& _fileName,
                     const string& _fileModes,
                     const string& _accessModes)
: fileName(_fileName),
  fileModes(_fileModes),
  accessModes(_accessModes),
  valid(false),
  initDepth(0),
  rootRef(LUA_NOREF),
  currentRef(LUA_NOREF)
{
	L = lua_open();
}


LuaParser::~LuaParser()
{
	if (L != NULL) {
		lua_close(L);
	}
	set<LuaTable*>::iterator it;
	for (it = tables.begin(); it != tables.end(); ++it) {
		LuaTable& table = **it;
		table.parser  = NULL;
		table.L       = NULL;
		table.isValid = false;
		table.refnum  = LUA_NOREF;
	}
}


/******************************************************************************/

void LuaParser::PushParam()
{
	if (L == NULL) { return; }
	if (initDepth > 0) {
		lua_rawset(L, -3);
	} else {
		lua_rawset(L, LUA_GLOBALSINDEX);
	}
}


void LuaParser::NewTable(const string& name)
{
	if (L == NULL) { return; }
	lua_pushstring(L, name.c_str());
	lua_newtable(L);
	initDepth++;
}


void LuaParser::NewTable(int index)
{
	if (L == NULL) { return; }
	lua_pushnumber(L, index);
	lua_newtable(L);
	initDepth++;
}


void LuaParser::EndTable()
{
	if (L == NULL) { return; }
	assert(initDepth > 0);
	initDepth--;
	PushParam();
}


/******************************************************************************/

void LuaParser::AddFunc(const string& key, int (*func)(lua_State*))
{
	if (L == NULL)    { return; }
	if (func == NULL) { return; }
	lua_pushstring(L, key.c_str());
	lua_pushcfunction(L, func);
	PushParam();
}


void LuaParser::AddParam(const string& key, const string& value)
{
	if (L == NULL) { return; }
	lua_pushstring(L, key.c_str());
	lua_pushstring(L, value.c_str());
	PushParam();
}


void LuaParser::AddParam(const string& key, float value)
{
	if (L == NULL) { return; }
	lua_pushstring(L, key.c_str());
	lua_pushnumber(L, value);
	PushParam();
}


void LuaParser::AddParam(const string& key, int value)
{
	if (L == NULL) { return; }
	lua_pushstring(L, key.c_str());
	lua_pushnumber(L, value);
	PushParam();
}


void LuaParser::AddParam(const string& key, bool value)
{
	if (L == NULL) { return; }
	lua_pushstring(L, key.c_str());
	lua_pushboolean(L, value);
	PushParam();
}


/******************************************************************************/

void LuaParser::AddFunc(int key, int (*func)(lua_State*))
{
	if (L == NULL)    { return; }
	if (func == NULL) { return; }
	lua_pushnumber(L, key);
	lua_pushcfunction(L, func);
	PushParam();
}


void LuaParser::AddParam(int key, const string& value)
{
	if (L == NULL) { return; }
	lua_pushnumber(L, key);
	lua_pushstring(L, value.c_str());
	PushParam();
}


void LuaParser::AddParam(int key, float value)
{
	if (L == NULL) { return; }
	lua_pushnumber(L, key);
	lua_pushnumber(L, value);
	PushParam();
}


void LuaParser::AddParam(int key, int value)
{
	if (L == NULL) { return; }
	lua_pushnumber(L, key);
	lua_pushnumber(L, value);
	PushParam();
}


void LuaParser::AddParam(int key, bool value)
{
	if (L == NULL) { return; }
	lua_pushnumber(L, key);
	lua_pushboolean(L, value);
	PushParam();
}


/******************************************************************************/

bool LuaParser::Execute()
{
	if (L == NULL) {
		errorLog = "could not initialize LUA library";
		return false;
	}

	assert(initDepth == 0);

	string code;
	CFileHandler fh(fileName, fileModes);
	if (!fh.LoadStringData(code)) {
		errorLog = "could not open file: " + fileName;
		lua_close(L);
		L = NULL;
		return false;
	}

	SetupStdLibs(L);

	NewTable("Spring");
	AddFunc("Echo", Echo);
	AddFunc("TimeCheck", TimeCheck);
	EndTable();

	NewTable("VFS");
	AddFunc("DirList",    DirList);
	AddFunc("Include",    Include);
	AddFunc("LoadFile",   LoadFile);
	AddFunc("FileExists", FileExists);
	EndTable();

	int error;
	error = luaL_loadbuffer(L, code.c_str(), code.size(), fileName.c_str());
	if (error != 0) {
		errorLog = lua_tostring(L, -1);
		logOutput.Print("error = %i, %s, %s\n",
		                error, fileName.c_str(), errorLog.c_str());
		lua_close(L);
		L = NULL;
		return false;
	}

	currentParser = this;

	error = lua_pcall(L, 0, 1, 0);

	currentParser = NULL;

	if (error != 0) {
		errorLog = lua_tostring(L, -1);
		logOutput.Print("error = %i, %s, %s\n",
		                error, fileName.c_str(), errorLog.c_str());
		lua_close(L);
		L = NULL;
		return false;
	}

	if (!lua_istable(L, 1)) {
		errorLog = "missing return table from " + fileName + "\n";
		logOutput.Print("missing return table from %s\n", fileName.c_str());
		lua_close(L);
		L = NULL;
		return false;
	}

	rootRef = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_settop(L, 0);

	valid = true;

	return true;
}


void LuaParser::AddTable(LuaTable* tbl)
{
	tables.insert(tbl);
}


void LuaParser::RemoveTable(LuaTable* tbl)
{
	tables.erase(tbl);
}


LuaTable LuaParser::GetRoot()
{
	return LuaTable(this);
}


/******************************************************************************/
/******************************************************************************/
//
//  call-outs
//

int LuaParser::Echo(lua_State* L)
{
	// copied from lua/src/lib/lbaselib.c
	string msg = "";
	const int args = lua_gettop(L); // number of arguments

	lua_getglobal(L, "tostring");

	for (int i = 1; i <= args; i++) {
		const char *s;
		lua_pushvalue(L, -1);     // function to be called
		lua_pushvalue(L, i);      // value to print
		lua_call(L, 1, 1);
		s = lua_tostring(L, -1);  // get result
		if (s == NULL) {
			return luaL_error(L, "`tostring' must return a string to `print'");
		}
		if (i > 1) {
			msg += ", ";
		}
		msg += s;
		lua_pop(L, 1);            // pop result
	}
	logOutput.Print(msg);

	if ((args != 1) || !lua_istable(L, 1)) {
		return 0;
	}

	// print solo tables (array style)
	msg = "TABLE: ";
	bool first = true;
	const int table = 1;
	for (lua_pushnil(L); lua_next(L, table) != 0; lua_pop(L, 1)) {
		if (lua_israwnumber(L, -2)) {  // only numeric keys
			const char *s;
			lua_pushvalue(L, -3);     // function to be called
			lua_pushvalue(L, -2	);    // value to print
			lua_call(L, 1, 1);
			s = lua_tostring(L, -1);  // get result
			if (s == NULL) {
				return luaL_error(L, "`tostring' must return a string to `print'");
			}
			if (!first) {
				msg += ", ";
			}
			msg += s;
			first = false;
			lua_pop(L, 1);            // pop result
		}
	}
	logOutput.Print(msg);

	return 0;
}


int LuaParser::TimeCheck(lua_State* L)
{	
	if (!lua_isstring(L, 1) || !lua_isfunction(L, 2)) {
		luaL_error(L, "Invalid arguments to TimeCheck('string', func, ...)");
	}
	const string name = lua_tostring(L, 1);
	lua_remove(L, 1);
	const Uint32 startTime = SDL_GetTicks();
	const int error = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);
	if (error != 0) {
		const string errmsg = lua_tostring(L, -1);
		lua_pop(L, 1);
		luaL_error(L, errmsg.c_str());
	}
	const Uint32 endTime = SDL_GetTicks();
	const float elapsed = 1.0e-3f * (float)(endTime - startTime);
	logOutput.Print("%s %f", name.c_str(), elapsed);
	return lua_gettop(L);
}


/******************************************************************************/

int LuaParser::DirList(lua_State* L)
{
	if (currentParser == NULL) {
		luaL_error(L, "invalid call to DirList() after execution");
	}

	const string dir = luaL_checkstring(L, 1);
	// keep searches within the Spring directory
	if ((dir[0] == '/') || (dir[0] == '\\') ||
	    ((dir.size() >= 2) && (dir[1] == ':'))) {
		return 0;
	}
	const string pat = luaL_optstring(L, 2, "*");
	string modes = luaL_optstring(L, 3, currentParser->accessModes.c_str());
	modes = CFileHandler::AllowModes(modes, currentParser->accessModes);

	const vector<string> files = CFileHandler::DirList(dir, pat, modes);

	lua_newtable(L);
	int count = 0;	
	vector<string>::const_iterator fi;
	for (fi = files.begin(); fi != files.end(); ++fi) {
		count++;
		lua_pushnumber(L, count);
		lua_pushstring(L, fi->c_str());
		lua_rawset(L, -3);
	}
	lua_pushstring(L, "n");
	lua_pushnumber(L, count);
	lua_rawset(L, -3);

	return 1;
}


/******************************************************************************/

int LuaParser::Include(lua_State* L)
{
	if (currentParser == NULL) {
		luaL_error(L, "invalid call to Include() after execution");
	}

	// filename [, fenv]
	const string filename = luaL_checkstring(L, 1);
	string modes = luaL_optstring(L, 3, currentParser->accessModes.c_str());
	modes = CFileHandler::AllowModes(modes, currentParser->accessModes);

	CFileHandler fh(filename, modes);
	if (!fh.FileExists()) {
		char buf[1024];
		SNPRINTF(buf, sizeof(buf),
		         "Include() file missing '%s'\n", filename.c_str());
		lua_pushstring(L, buf);
 		lua_error(L);
	}

	string code;
	if (!fh.LoadStringData(code)) {
		char buf[1024];
		SNPRINTF(buf, sizeof(buf),
		         "Include() could not load '%s'\n", filename.c_str());
		lua_pushstring(L, buf);
 		lua_error(L);
	}

	int error = luaL_loadbuffer(L, code.c_str(), code.size(), filename.c_str());
	if (error != 0) {
		char buf[1024];
		SNPRINTF(buf, sizeof(buf), "error = %i, %s, %s\n",
		         error, filename.c_str(), lua_tostring(L, -1));
		lua_pushstring(L, buf);
		lua_error(L);
	}

	// set the chunk's fenv to the current fenv, or a user table
	if (lua_istable(L, 2)) {
		lua_pushvalue(L, 2); // user fenv
	} else {
		LuaUtils::PushCurrentFuncEnv(L, __FUNCTION__);
	}

	// set the include fenv to the current function's fenv
	if (lua_setfenv(L, -2) == 0) {
		luaL_error(L, "Include(): error with setfenv");
	}

	const int paramTop = lua_gettop(L) - 1;	

	error = lua_pcall(L, 0, LUA_MULTRET, 0);

	if (error != 0) {
		char buf[1024];
		SNPRINTF(buf, sizeof(buf), "error = %i, %s, %s\n",
		         error, filename.c_str(), lua_tostring(L, -1));
		lua_pushstring(L, buf);
		lua_error(L);
	}

	currentParser->accessedFiles.insert(StringToLower(filename));

	return lua_gettop(L) - paramTop;
}


/******************************************************************************/

int LuaParser::LoadFile(lua_State* L)
{
	if (currentParser == NULL) {
		luaL_error(L, "invalid call to LoadFile() after execution");
	}

	const string filename = luaL_checkstring(L, 1);
	string modes = luaL_optstring(L, 2, currentParser->accessModes.c_str());
	modes = CFileHandler::AllowModes(modes, currentParser->accessModes);

	CFileHandler fh(filename, modes);
	if (!fh.FileExists()) {
		lua_pushnil(L);
		lua_pushstring(L, "missing file");
		return 2;
	}
	string data;
	if (!fh.LoadStringData(data)) {
		lua_pushnil(L);
		lua_pushstring(L, "could not load data");
		return 2;
	}
	lua_pushstring(L, data.c_str());

	currentParser->accessedFiles.insert(StringToLower(filename));

	return 1;
}


int LuaParser::FileExists(lua_State* L)
{
	if (currentParser == NULL) {
		luaL_error(L, "invalid call to FileExists() after execution");
	}
	const string filename = luaL_checkstring(L, 1);
	CFileHandler fh(filename, currentParser->accessModes);
	lua_pushboolean(L, fh.FileExists());
	return 1;
}


/******************************************************************************/
/******************************************************************************/
//
//  LuaTable
//

LuaTable::LuaTable()
: isValid(false),
  path(""),
  parser(NULL),
  L(NULL),
  refnum(LUA_NOREF)
{
}


LuaTable::LuaTable(LuaParser* _parser)
{
	assert(_parser != NULL);

	isValid = true;
	path    = "ROOT";
	parser  = _parser;
  L       = parser->L;
	refnum  = parser->rootRef;
	
	if (PushTable()) {
		lua_pushvalue(L, -1); // copy
		refnum = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
	 	refnum = LUA_NOREF;
	}
	isValid = (refnum != LUA_NOREF);

	parser->AddTable(this);
}


LuaTable::LuaTable(const LuaTable& tbl)
{
	parser = tbl.parser;
	L      = tbl.L;
	path   = tbl.path;

	if (tbl.PushTable()) {
		lua_pushvalue(L, -1); // copy
		refnum = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
		refnum = LUA_NOREF;
	}	
	isValid = (refnum != LUA_NOREF);

	if (parser) {
		parser->AddTable(this);
	}
}


LuaTable& LuaTable::operator=(const LuaTable& tbl)
{
	if (parser && (refnum != LUA_NOREF) && (parser->currentRef == refnum)) {
		lua_settop(L, 0);
		parser->currentRef = LUA_NOREF;
	}

	if (parser != tbl.parser) {
		if (parser != NULL) {
			parser->RemoveTable(this);
		}
		if (L && (refnum != LUA_NOREF)) {
			luaL_unref(L, LUA_REGISTRYINDEX, refnum);
		}
		parser = tbl.parser;
		if (parser != NULL) {
			parser->AddTable(this);
		}
	}

	L    = tbl.L;
	path = tbl.path;
	
	if (tbl.PushTable()) {
		lua_pushvalue(L, -1); // copy
		refnum = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
		refnum = LUA_NOREF;
	}	

	isValid = (refnum != LUA_NOREF);

	return *this;
}


LuaTable LuaTable::SubTable(int key) const
{
	LuaTable subTable;
	char buf[32];
	SNPRINTF(buf, 32, "[%i]", key);
	subTable.path = path + buf;

	if (!PushTable()) {
		return subTable;
	}

	lua_pushnumber(L, key);
	lua_gettable(L, -2);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return subTable;
	}

	subTable.parser  = parser;
	subTable.L       = L;
	subTable.refnum  = luaL_ref(L, LUA_REGISTRYINDEX);
	subTable.isValid = (subTable.refnum != LUA_NOREF);

	parser->AddTable(&subTable);

	return subTable;
}


LuaTable LuaTable::SubTable(const string& mixedKey) const
{
	const string key = StringToLower(mixedKey);

	LuaTable subTable;
	subTable.path = path + "." + key;

	if (!PushTable()) {
		return subTable;
	}

	lua_pushstring(L, key.c_str());
	lua_gettable(L, -2);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return subTable;
	}

	subTable.parser  = parser;
	subTable.L       = L;
	subTable.refnum  = luaL_ref(L, LUA_REGISTRYINDEX);
	subTable.isValid = (subTable.refnum != LUA_NOREF);

	parser->AddTable(&subTable);

	return subTable;
}


LuaTable::~LuaTable()
{
	if (L && (refnum != LUA_NOREF)) {
		luaL_unref(L, LUA_REGISTRYINDEX, refnum);
		if (parser && (parser->currentRef == refnum)) {
			lua_settop(L, 0);
			parser->currentRef = LUA_NOREF;
		}
	}
	if (parser) {
		parser->RemoveTable(this);
	}
}


/******************************************************************************/

bool LuaTable::PushTable() const
{
	if (!isValid) {
		return false;
	}

	if ((refnum != LUA_NOREF) && (parser->currentRef == refnum)) {
		if (!lua_istable(L, -1)) {
			logOutput.Print("Internal Error: LuaTable::PushTable() = %s\n",
			                path.c_str());
			parser->currentRef = LUA_NOREF;
			lua_settop(L, 0);
			return false;
		}
		return true;
	}

	lua_settop(L, 0);

	lua_rawgeti(L, LUA_REGISTRYINDEX, refnum);
	if (!lua_istable(L, -1)) {
		isValid = false;
		parser->currentRef = LUA_NOREF;
		lua_settop(L, 0);
		return false;
	}

	parser->currentRef = refnum;

	return true;
}


bool LuaTable::PushValue(int key) const
{
	if (!PushTable()) {
		return false;
	}
	lua_pushnumber(L, key);
	lua_gettable(L, -2);
	if (lua_isnoneornil(L, -1)) {
		lua_pop(L, 1);
		return false;
	}
	return true;	
}


bool LuaTable::PushValue(const string& mixedKey) const
{
	const string key = StringToLower(mixedKey);
	if (!PushTable()) {
		return false;
	}
	lua_pushstring(L, key.c_str());
	lua_gettable(L, -2);
	if (lua_isnoneornil(L, -1)) {
		lua_pop(L, 1);
		return false;
	}
	return true;	
}


/******************************************************************************/
/******************************************************************************/
//
//  Key existance testing
//

bool LuaTable::KeyExists(int key) const
{
	if (!PushValue(key)) {
		return false;
	}
	lua_pop(L, 1);
	return true;
}


bool LuaTable::KeyExists(const string& key) const
{
	if (!PushValue(key)) {
		return false;
	}
	lua_pop(L, 1);
	return true;
}


/******************************************************************************/
/******************************************************************************/
//
//  Key list functions
//

bool LuaTable::GetKeys(vector<int>& data) const
{
	if (!PushTable()) {
		return false;
	}
	const int table = lua_gettop(L);
	for (lua_pushnil(L); lua_next(L, table) != 0; lua_pop(L, 1)) {
		if (lua_israwnumber(L, -2)) {
			const int value = (int)lua_tonumber(L, -2);
			data.push_back(value);
		}
	}
	std::sort(data.begin(), data.end());
	return true;
}


bool LuaTable::GetKeys(vector<string>& data) const
{
	if (!PushTable()) {
		return false;
	}
	const int table = lua_gettop(L);
	for (lua_pushnil(L); lua_next(L, table) != 0; lua_pop(L, 1)) {
		if (lua_israwstring(L, -2)) {
			const string value = lua_tostring(L, -2);
			data.push_back(value);
		}
	}
	std::sort(data.begin(), data.end());
	return true;
}


/******************************************************************************/
/******************************************************************************/
//
//  Map functions
//

bool LuaTable::GetMap(map<int, float>& data) const
{
	if (!PushTable()) {
		return false;
	}
	const int table = lua_gettop(L);
	for (lua_pushnil(L); lua_next(L, table) != 0; lua_pop(L, 1)) {
		if (lua_israwnumber(L, -2) && lua_isnumber(L, -1)) {
			const int   key   =   (int)lua_tonumber(L, -2);
			const float value = (float)lua_tonumber(L, -1);
			data[key] = value;
		}
	}
	return true;
}


bool LuaTable::GetMap(map<int, string>& data) const
{
	if (!PushTable()) {
		return false;
	}
	const int table = lua_gettop(L);
	for (lua_pushnil(L); lua_next(L, table) != 0; lua_pop(L, 1)) {
		if (lua_israwnumber(L, -2) && lua_isstring(L, -1)) {
			const int    key   = (int)lua_tonumber(L, -2);
			const string value = lua_tostring(L, -1);
			data[key] = value;
		}
	}
	return true;
}


bool LuaTable::GetMap(map<string, float>& data) const
{
	if (!PushTable()) {
		return false;
	}
	const int table = lua_gettop(L);
	for (lua_pushnil(L); lua_next(L, table) != 0; lua_pop(L, 1)) {
		if (lua_israwstring(L, -2) && lua_isnumber(L, -1)) {
			const string key   = lua_tostring(L, -2);
			const float  value = (float)lua_tonumber(L, -1);
			data[key] = value;
		}
	}
	return true;
}


bool LuaTable::GetMap(map<string, string>& data) const
{
	if (!PushTable()) {
		return false;
	}
	const int table = lua_gettop(L);
	for (lua_pushnil(L); lua_next(L, table) != 0; lua_pop(L, 1)) {
		if (lua_israwstring(L, -2) && lua_isstring(L, -1)) {
			const string key   = lua_tostring(L, -2);
			const string value = lua_tostring(L, -1);
			data[key] = value;
		}
	}
	return true;
}


/******************************************************************************/
/******************************************************************************/
//
//  Parsing utilities
//

static bool ParseTableFloat(lua_State* L,
                            int tableIndex, int index, float& value)
{
	lua_pushnumber(L, index);
	lua_gettable(L, tableIndex);
	if (!lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		return false;
	}
	value = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return true;
}


static bool ParseFloat3(lua_State* L, int index, float3& value)
{
	if (lua_istable(L, index)) {
		const int table = (index > 0) ? index : lua_gettop(L) + index + 1;
		if (ParseTableFloat(L, table, 1, value.x) &&
		    ParseTableFloat(L, table, 2, value.y) &&
		    ParseTableFloat(L, table, 3, value.z)) {
			return true;
		}
	}
	else if (lua_isstring(L, index)) {
		const int count = sscanf(lua_tostring(L, index), "%f %f %f",
		                         &value.x, &value.y, &value.z);
		if (count == 3) {
			return true;
		}
	}
	return false;
}


static bool ParseBoolean(lua_State* L, int index, bool& value)
{
	if (lua_isboolean(L, index)) {
		value = lua_toboolean(L, index);
		return true;
	}
	else if (lua_isnumber(L, index)) {
		value = ((float)lua_tonumber(L, index) != 0.0f);
		return true;
	}
	else if (lua_isstring(L, index)) {
		const string str = StringToLower(lua_tostring(L, index));
		if ((str == "1") || (str == "true")) {
			value = true;
			return true;
		}
		if ((str == "0") || (str == "false")) {
			value = false;
			return true;
		}
	}
	return false;
}


/******************************************************************************/
/******************************************************************************/
//
//  String key functions
//

int LuaTable::GetInt(const string& key, int def) const
{
	if (!PushValue(key)) {
		return def;
	}
	if (!lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		return def;
	}
	const int value = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return value;
}


bool LuaTable::GetBool(const string& key, bool def) const
{
	if (!PushValue(key)) {
		return def;
	}
	bool value;
	if (!ParseBoolean(L, -1, value)) {
		lua_pop(L, 1);
		return def;
	}
	lua_pop(L, 1);
	return value;
}


float LuaTable::GetFloat(const string& key, float def) const
{
	if (!PushValue(key)) {
		return def;
	}
	if (!lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		return def;
	}
	const float value = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return value;
}


float3 LuaTable::GetFloat3(const string& key, const float3& def) const
{
	if (!PushValue(key)) {
		return def;
	}
	float3 value;
	if (!ParseFloat3(L, -1, value)) {
		lua_pop(L, 1);
		return def;
	}
	lua_pop(L, 1);
	return value;
}


string LuaTable::GetString(const string& key, const string& def) const
{
	if (!PushValue(key)) {
		return def;
	}
	if (!lua_isstring(L, -1)) {
		lua_pop(L, 1);
		return def;
	}
	const string value = lua_tostring(L, -1);
	lua_pop(L, 1);
	return value;
}


/******************************************************************************/
/******************************************************************************/
//
//  Number key functions
//

int LuaTable::GetInt(int key, int def) const
{
	if (!PushValue(key)) {
		return def;
	}
	if (!lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		return def;
	}
	const int value = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return value;
}


bool LuaTable::GetBool(int key, bool def) const
{
	if (!PushValue(key)) {
		return def;
	}
	bool value;
	if (!ParseBoolean(L, -1, value)) {
		lua_pop(L, 1);
		return def;
	}
	lua_pop(L, 1);
	return value;
}


float LuaTable::GetFloat(int key, float def) const
{
	if (!PushValue(key)) {
		return def;
	}
	if (!lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		return def;
	}
	const float value = (float)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return value;
}


float3 LuaTable::GetFloat3(int key, const float3& def) const
{
	if (!PushValue(key)) {
		return def;
	}
	float3 value;
	if (!ParseFloat3(L, -1, value)) {
		lua_pop(L, 1);
		return def;
	}
	lua_pop(L, 1);
	return value;
}


string LuaTable::GetString(int key, const string& def) const
{
	if (!PushValue(key)) {
		return def;
	}
	if (!lua_isstring(L, -1)) {
		lua_pop(L, 1);
		return def;
	}
	const string value = lua_tostring(L, -1);
	lua_pop(L, 1);
	return value;
}


/******************************************************************************/
/******************************************************************************/
