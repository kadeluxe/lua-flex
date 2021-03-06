#include "hooks.h"
#include "luawatcher.h"
#include "lau/lau.h"
#include "lua.hpp"
#include "classes/structures.h"
#include "classes/color.h"
#include "classes/usercmd.h"
#include "classes/engines.h"
#include "init.h"

#include "classes/cliententitylist.h"
#include "classes/globals.h"
#include "classes/entities.h"
#include "classes/engineclient.h"
#include "classes/matsystemsurface.h"
#include "classes/clienttools.h"
#include "classes/panelwrapper.h"
#include "classes/angle.h"
#include "classes/math.h"

#include <TlHelp32.h>

#define CREATEMOVE_INDEX (version == CSGO ? 24 : 21)
#define SETLOCALVIEWANGLES_INDEX (13)
#define PAINTTRAVERSE_INDEX (41)
#define PAINT_INDEX (13)
#define CLCREATEMOVE_INDEX (21)
__declspec(dllimport) void __cdecl ConColorMsg(const Color &, const char *, ...);

VTable *HOOKS::client_mode_vt = 0;
VTable *HOOKS::prediction_vt = 0;
VTable *HOOKS::panel_vt = 0;
VTable *HOOKS::client_vt = 0;

HHOOK KeyboardHookLLNext;

extern EngineVersion version;
extern unsigned long font;

using namespace csgolua;
using namespace HOOKS;

bool bSendPacket = true;

void __fastcall CSGOCLCreateMove_Hook2(CClient *ths, void *, int a, float b, bool c)
{
	typedef void(__thiscall *OriginalFn)(CClient *, int, float, bool);
	return OriginalFn(client_vt->getold(CLCREATEMOVE_INDEX))(ths, a, b, c);
}

void (__cdecl *CL_Move)(float extra_samples, bool bFinalTick) = 0;
void(__cdecl *CL_SendMove)(void) = 0;


void *CL_MoveRealReturn;

bool INSIDE_HOOK = false;
__declspec(naked) void CL_MoveReturn(void)
{
	/*
	__asm pushad;

	INSIDE_HOOK = true;

	while (current-- != 0)
	{
		bOverrideSendPacket = OVERRIDE_TRUE;
		CL_Move(0, true);
	}
	current = max_cmds;

	INSIDE_HOOK = false;

	__asm popad;
	*/
	__asm jmp CL_MoveRealReturn;
}

void *CL_MoveReturnAddr = &CL_MoveReturn;

__declspec(naked) void __fastcall CSGOCLCreateMove_Hook(CClient *ths, void *, int a, float b, bool c)
{ // big hack codes coming in
	void *cl_ret;
	__asm
	{
		push eax;

		mov al, INSIDE_HOOK; // this is eax ok
		cmp al, 1;
		je noreturn;

		mov eax, ebp;
		add eax, 4; // return address

		push esi;
		mov esi, [eax];
		mov CL_MoveRealReturn, esi;
		mov esi, CL_MoveReturnAddr;
		mov [eax], esi;
		pop esi;

	noreturn:

		mov eax, [esp + 4];
		mov cl_ret, eax;
		pop eax;
		add esp, 4;
		call CSGOCLCreateMove_Hook2;
		mov bl, bSendPacket;

		jmp cl_ret;
	}
}


void __fastcall CLCreateMove_Hook(CClient *ths, void *, int a, float b, bool c)
{

	typedef void(__thiscall *OriginalFn)(CClient *, int, float, bool);
	OriginalFn(client_vt->getold(CLCREATEMOVE_INDEX))(ths, a, b, c);
	void *_ebp = 0;
	__asm mov _ebp, ebp
	*(*(unsigned char **)_ebp - 1) = bSendPacket;
}

extern Color print_color;


bool __fastcall CreateMove_Hook(ClientModeShared *ths, void*, float frametime, CUserCmd *cmd)
{
	typedef bool(__thiscall *CreateMoveFn)(ClientModeShared *, float frametime, CUserCmd *cmd);
	bool ret = CreateMoveFn(client_mode_vt->getold(CREATEMOVE_INDEX))(ths, frametime, cmd);

	if (cmd)
	{
		auto state = structs.L->GetState();

		if (structs.L->PushHookCall())
		{

			lua_pushstring(state, "CreateMove");
			LPush(state, cmd, "CUserCmd");
			lua_pushnumber(state, frametime);

			const char *err = structs.L->SafeCall(3, 0);
			if (err)
			{
				ConColorMsg(print_color, "%s\n", err);
			}

		}
	}

	cmd->angles.Normalize();
	cmd->angles.Clamp();

	return ret;
}


void __fastcall SetLocalViewAngles_Hook(CPrediction *ths, void *, QAngle &target_ang)
{

	QAngle ang = target_ang;

	typedef void(__thiscall *OriginalFn)(void *, QAngle &);
	auto me = structs.entity_list->GetClientEntity(structs.engine->GetLocalPlayer());

	if (me)
	{

		lua_State *state = structs.L->GetState();


		if (structs.L->PushHookCall())
		{

			lua_pushstring(state, "SetLocalViewAngles");
			LPush(state, ang, "Angle");

			const char *err = structs.L->SafeCall(2, 1);

			if (err)
				ConColorMsg(print_color, "%s\n", err);

			else
			{

				QAngle &GetAngle(lua_State *L, int where = -1);
				if (lua_type(state, -1) == LUA_TUSERDATA)
					ang = GetAngle(state, -1);

				lua_pop(state, 1);

			}

		}


	}
	return OriginalFn(prediction_vt->getold(SETLOCALVIEWANGLES_INDEX))(ths, ang);
}


void __fastcall PaintTraverse_Hook(VPanelWrapper *ths, void *, unsigned int panel, bool something1, bool something2)
{
	static CFileWatcher *watcher = 0;
	if (!watcher)
		watcher = new CFileWatcher("C:\\aim-flex\\lua\\");

	typedef void(__thiscall *OriginalFn)(void *, ulong, bool, bool);
	OriginalFn(panel_vt->getold(PAINTTRAVERSE_INDEX))(ths, panel, something1, something2);
	if (!strcmp(ths->GetName(panel), version == GARRYSMOD ? "OverlayPopupPanel" : "MatSystemTopPanel"))
	{
		auto state = structs.L->GetState();

		if (structs.L->PushHookCall())
		{

			lua_pushstring(state, "Paint");

			const char *err = structs.L->SafeCall(1);

			if (err)
				ConColorMsg(print_color, "%s\n", err);
		}

		if (watcher->Query())
			structs.L->Init();

	}
}

void HOOKS::InitHooks() 
{

	client_vt = new VTable(structs.client);
	if (version == CSGO)
		client_vt->hook(CLCREATEMOVE_INDEX, &CSGOCLCreateMove_Hook);
	else
		client_vt->hook(CLCREATEMOVE_INDEX, &CLCreateMove_Hook);

	prediction_vt = new VTable(structs.prediction);
	prediction_vt->hook(SETLOCALVIEWANGLES_INDEX, &SetLocalViewAngles_Hook);

	client_mode_vt = new VTable(structs.client_mode);

	client_mode_vt->hook(CREATEMOVE_INDEX, &CreateMove_Hook);

	panel_vt = new VTable(structs.panel);
	panel_vt->hook(PAINTTRAVERSE_INDEX, &PaintTraverse_Hook);

}
