/*
rF2 TeleHUD Plugin

Author: dbosst <dbosst@gmail.com>
Date:   June 2016
URL:    https://github.com/dbosst/rfactor2-telehud

Borrowed most of the framework from the DeltaBest plugin by cosimo

*/


#include "TeleHUD.hpp"

// plugin information

extern "C" __declspec(dllexport)
	const char * __cdecl GetPluginName()                   { return PLUGIN_NAME; }

extern "C" __declspec(dllexport)
	PluginObjectType __cdecl GetPluginType()               { return PO_INTERNALS; }

extern "C" __declspec(dllexport)
	int __cdecl GetPluginVersion()                         { return 6; }

extern "C" __declspec(dllexport)
	PluginObject * __cdecl CreatePluginObject()            { return((PluginObject *) new teleHUDPlugin); }

extern "C" __declspec(dllexport)
	void __cdecl DestroyPluginObject(PluginObject *obj)    { delete((teleHUDPlugin *)obj); }

bool in_realtime = false;              /* Are we in cockpit? As opposed to monitor */
bool green_flag = false;               /* Is the race in green flag condition? */
int key_switch = 2;                /* Enabled/disabled state by keyboard action */
bool displayed_welcome = false;        /* Whether we displayed the "plugin enabled" welcome message */

struct teleData {
	double mDrag;                  // drag
	double mFrontDownforce;        // front downforce
	double mRearDownforce;         // rear downforce
	TelemWheelV01 mWheel[4];		// FL,FR,RL,RR
} lastTeleData;

struct PluginConfig {

	bool bar_enabled;
	unsigned int bar_left;
	unsigned int bar_top;
	unsigned int bar_width;
	unsigned int bar_height;
	unsigned int bar_gutter;

	bool time_enabled;
	bool hires_updates;
	unsigned int time_top;
	unsigned int time_width;
	unsigned int time_height;
	unsigned int time_font_size;
	char time_font_name[FONT_NAME_MAXLEN];

	unsigned int keyboard_magic;
	unsigned int keyboard_reset;
	unsigned int keyboard_incrscale;
	unsigned int keyboard_decrscale;
} config;

#ifdef ENABLE_LOG
FILE* out_file = NULL;
#endif

// DirectX 9 objects, to render some text on screen
LPD3DXFONT g_Font = NULL;
D3DXFONT_DESC FontDesc = {
	DEFAULT_FONT_SIZE, 0, 500, 0, false, DEFAULT_CHARSET,
	OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_PITCH, DEFAULT_FONT_NAME
};
RECT FontPosition1, ShadowPosition1;
RECT FontPosition2, ShadowPosition2;
RECT FontPosition3, ShadowPosition3;
LPD3DXSPRITE bar = NULL;
LPDIRECT3DTEXTURE9 texture = NULL;

//
// teleHUDPlugin class
//

void teleHUDPlugin::WriteLog(const char * const msg)
{
#ifdef ENABLE_LOG
	if (out_file == NULL)
		out_file = fopen(LOG_FILE, "a");

	if (out_file != NULL)
		fprintf(out_file, "%s\n", msg);
#endif /* ENABLE_LOG */
}

void teleHUDPlugin::Startup(long version)
{
	// default HW control enabled to true
	mEnabled = true;
#ifdef ENABLE_LOG
	WriteLog("--STARTUP--");
#endif /* ENABLE_LOG */
}

void teleHUDPlugin::StartSession()
{
#ifdef ENABLE_LOG
	WriteLog("--STARTSESSION--");
#endif /* ENABLE_LOG */
	//in_realtime = true;
	displayed_welcome = false;
}

void teleHUDPlugin::EndSession()
{
	mET = 0.0f;
	in_realtime = false;
#ifdef ENABLE_LOG
	WriteLog("--ENDSESSION--");
	if (out_file) {
		fclose(out_file);
		out_file = NULL;
	}
#endif /* ENABLE_LOG */
}

void teleHUDPlugin::Load()
{
#ifdef ENABLE_LOG
	WriteLog("--LOAD--");
#endif /* ENABLE_LOG */
}

void teleHUDPlugin::Unload()
{
#ifdef ENABLE_LOG
	WriteLog("--UNLOAD--");
#endif /* ENABLE_LOG */
}

void teleHUDPlugin::EnterRealtime()
{
	// start up timer every time we enter realtime
	mET = 0.0f;
	in_realtime = true;
#ifdef ENABLE_LOG
	WriteLog("---ENTERREALTIME---");
#endif /* ENABLE_LOG */
}

void teleHUDPlugin::ExitRealtime()
{
	in_realtime = false;

#ifdef ENABLE_LOG
	WriteLog("---EXITREALTIME---");
#endif /* ENABLE_LOG */
}

bool teleHUDPlugin::NeedToDisplay()
{
	// If we're in the monitor or replay,
	// no tele hud should be displayed
	if (! in_realtime)
		return false;

	// Option might be disabled by the user (TAB)
	if (key_switch != 2)
		return false;

	// If we are in any race/practice phase that's not
	// green flag, we don't need or want tele HUD displayed
///	if (! green_flag)
///		return false;

///	/* We can't display a tele HUD until we have a best lap recorded */
///	if (! best_lap.final)
///		return false;

	return true;
}

void teleHUDPlugin::UpdateTelemetry(const TelemInfoV01 &info)
{
	// debounce since using async
	if (KEY_DOWN(config.keyboard_magic)) {
		if (key_switch == -2) {
			key_switch = 1;
		} else if (key_switch == 2) {
			key_switch = -1;
		}
	} else {
		if (key_switch == -1) {
			key_switch = -2;
		}
		else if (key_switch == 1) {
			key_switch = 2;
		}
	}

	if (! in_realtime)
		return;

	lastTeleData.mDrag = info.mDrag;
	lastTeleData.mFrontDownforce = info.mFrontDownforce;
	lastTeleData.mRearDownforce = info.mRearDownforce;
	int i;
	for (i = 0; i < 4; i++) {
		memcpy(&lastTeleData.mWheel[i], &info.mWheel[i], sizeof(info.mWheel[i]));
	}
	
}

void teleHUDPlugin::InitScreen(const ScreenInfoV01& info)
{
	long screen_width = info.mWidth;
	long screen_height = info.mHeight;

	LoadConfig(config, CONFIG_FILE);

	/* Now we know screen X/Y, we can place the text somewhere specific (in height).
	If everything is zero then apply our defaults. */

	if (config.time_width == 0)
		config.time_width = screen_width;
	if (config.time_height == 0)
		config.time_height = screen_height;
	if (config.time_top == 0)
		config.time_top = screen_height / 4.0;

	FontDesc.Height = config.time_font_size;
	sprintf(FontDesc.FaceName, config.time_font_name);

	D3DXCreateFontIndirect((LPDIRECT3DDEVICE9) info.mDevice, &FontDesc, &g_Font);
	assert(g_Font != NULL);

	D3DXCreateTextureFromFile((LPDIRECT3DDEVICE9) info.mDevice, TEXTURE_BACKGROUND, &texture);
	D3DXCreateSprite((LPDIRECT3DDEVICE9) info.mDevice, &bar);

	assert(texture != NULL);
	assert(bar != NULL);

#ifdef ENABLE_LOG
	WriteLog("---INIT SCREEN---");
#endif /* ENABLE_LOG */

}

void teleHUDPlugin::UninitScreen(const ScreenInfoV01& info)
{
	if (g_Font) {
		g_Font->Release();
		g_Font = NULL;
	}
	if (bar) {
		bar->Release();
		bar = NULL;
	}
	if (texture) {
		texture->Release();
		texture = NULL;
	}
#ifdef ENABLE_LOG
	WriteLog("---UNINIT SCREEN---");
#endif /* ENABLE_LOG */
}

void teleHUDPlugin::DeactivateScreen(const ScreenInfoV01& info)
{
#ifdef ENABLE_LOG
	WriteLog("---DEACTIVATE SCREEN---");
#endif /* ENABLE_LOG */
}

void teleHUDPlugin::ReactivateScreen(const ScreenInfoV01& info)
{
#ifdef ENABLE_LOG
	WriteLog("---REACTIVATE SCREEN---");
#endif /* ENABLE_LOG */
}

bool teleHUDPlugin::WantsToDisplayMessage( MessageInfoV01 &msgInfo )
{

	/* Wait until we're in realtime, otherwise
	the message is lost in space */
	if (! in_realtime)
		return false;

	/* We just want to display this message once in this rF2 session */
	if (displayed_welcome)
		return false;

	/* Tell how to toggle display through keyboard */
	msgInfo.mDestination = 0;
	msgInfo.mTranslate = 0;

	sprintf(msgInfo.mText, "teleHUD " TELE_HUD_VERSION " plugin enabled (CTRL + C to toggle");
	
	/* Don't do it anymore, just once per session */
	displayed_welcome = true;

	return true;
}

void teleHUDPlugin::DrawHUD(const ScreenInfoV01 &info)
{
	LPDIRECT3DDEVICE9 d3d = (LPDIRECT3DDEVICE9) info.mDevice;

	const float SCREEN_WIDTH    = info.mWidth;
	const float SCREEN_HEIGHT   = info.mHeight;
	const float SCREEN_CENTER   = SCREEN_WIDTH / 4.0;

	const float BAR_WIDTH       = config.bar_width;
	const float BAR_TOP         = config.bar_top;
	const float BAR_HEIGHT      = config.bar_height;
	const float BAR_TIME_GUTTER = config.bar_gutter;
	const float TIME_WIDTH      = config.time_width;
	const float TIME_HEIGHT     = config.time_height;

	// Computed positions, sizes
	// The -5 "compensates" for font height vs time box height difference
	const float TIME_TOP        = BAR_TOP;

	const D3DCOLOR BAR_COLOR    = D3DCOLOR_RGBA(0x50, 0x50, 0x50, 0xFF);

	RECT delta_size = { 0, 0, 0, BAR_HEIGHT - 2 };

	// Provide a default centered position in case user
	// disabled drawing of the bar
	double delta_posx = SCREEN_CENTER;
	double delta_posy = BAR_TOP + 1;
	double delta_posz = 0;
	delta_size.right = 1;

	// Draw the time text ("-0.18")
	if (config.time_enabled) {

		D3DCOLOR shadowColor = 0xC0585858;
		D3DCOLOR textColor = TextColor(1.0);

		char lp_teleHUD1[31] = "";
		char lp_teleHUD2[31] = "";
		char lp_teleHUD3[31] = "";

		float time_rect_center = delta_size.right;
		float left_edge = SCREEN_CENTER - BAR_WIDTH;
		float right_edge = SCREEN_CENTER + BAR_WIDTH;
		if (time_rect_center <= left_edge)
			time_rect_center = left_edge + 1;
		else if (time_rect_center >= right_edge)
			time_rect_center = right_edge - 1;

		RECT time_rect = { 0, 0, TIME_WIDTH, TIME_HEIGHT };
		D3DXVECTOR3 time_pos;
		
		time_pos.x = BAR_TIME_GUTTER;
		time_pos.y = TIME_TOP;
		time_pos.z = 0;
		time_rect.right = TIME_WIDTH;

		unsigned int tireloadFL = roundi(lastTeleData.mWheel[0].mTireLoad);
		unsigned int tireloadFR = roundi(lastTeleData.mWheel[1].mTireLoad);
		unsigned int tireloadRL = roundi(lastTeleData.mWheel[2].mTireLoad);
		unsigned int tireloadRR = roundi(lastTeleData.mWheel[3].mTireLoad);

		double tirewearFL = lastTeleData.mWheel[0].mWear * 100.0;
		double tirewearFR = lastTeleData.mWheel[1].mWear * 100.0;
		double tirewearRL = lastTeleData.mWheel[2].mWear * 100.0;
		double tirewearRR = lastTeleData.mWheel[3].mWear * 100.0;

		sprintf(lp_teleHUD1, "%5i %5i %6.2f%% %6.2f%%", tireloadFL, tireloadFR, tirewearFL, tirewearFR);
		sprintf(lp_teleHUD2, "%5i %5i %6.2f%% %6.2f%%", tireloadRL, tireloadRL, tirewearRL, tirewearRR);
		sprintf(lp_teleHUD3, "%+7.1f %+7.1f %+7.1f", lastTeleData.mFrontDownforce, lastTeleData.mRearDownforce, lastTeleData.mDrag);

		FontPosition1.left = time_pos.x;
		FontPosition1.top = time_pos.y;
		FontPosition1.right = FontPosition1.left + TIME_WIDTH;
		FontPosition1.bottom = FontPosition1.top + TIME_HEIGHT + 5;

		ShadowPosition1.left = FontPosition1.left + 2;
		ShadowPosition1.top = FontPosition1.top + 2;
		ShadowPosition1.right = FontPosition1.right;
		ShadowPosition1.bottom = FontPosition1.bottom;

		FontPosition2 = FontPosition1;
		ShadowPosition2 = ShadowPosition1;
		FontPosition2.top += TIME_HEIGHT + 5;
		ShadowPosition2.top += TIME_HEIGHT + 5;
		FontPosition2.bottom += TIME_HEIGHT + 5;
		ShadowPosition2.bottom += TIME_HEIGHT + 5;

		FontPosition3 = FontPosition2;
		ShadowPosition3 = ShadowPosition2;
		FontPosition3.top += TIME_HEIGHT + 5;
		ShadowPosition3.top += TIME_HEIGHT + 5;
		FontPosition3.bottom += TIME_HEIGHT + 5;
		ShadowPosition3.bottom += TIME_HEIGHT + 5;

		time_rect.left = FontPosition1.left > 0 ? FontPosition1.left : 0;
		time_rect.right = FontPosition1.right > 0 ? FontPosition1.right : 0;
		time_rect.top = FontPosition1.top - 5 > 0 ? FontPosition1.top - 5 : 0;
		time_rect.bottom = FontPosition3.bottom > 0 ? FontPosition3.bottom: 0;

		D3DXVECTOR3 boxarea;
		boxarea.x = time_rect.left;
		boxarea.y = time_rect.top;
		boxarea.z = 0;
		D3DCOLOR bar_grey = BAR_COLOR;
		bar->Begin(D3DXSPRITE_ALPHABLEND);
		bar->Draw(texture, &time_rect, NULL, &boxarea , bar_grey);
		bar->End();

		g_Font->DrawText(NULL, (LPCSTR)lp_teleHUD1, -1, &ShadowPosition1, DT_LEFT, shadowColor);
		g_Font->DrawText(NULL, (LPCSTR)lp_teleHUD1, -1, &FontPosition1, DT_LEFT, textColor);

		g_Font->DrawText(NULL, (LPCSTR)lp_teleHUD2, -1, &ShadowPosition2, DT_LEFT, shadowColor);
		g_Font->DrawText(NULL, (LPCSTR)lp_teleHUD2, -1, &FontPosition2, DT_LEFT, textColor);

		g_Font->DrawText(NULL, (LPCSTR)lp_teleHUD3, -1, &ShadowPosition3, DT_LEFT, shadowColor);
		g_Font->DrawText(NULL, (LPCSTR)lp_teleHUD3, -1, &FontPosition3, DT_LEFT, textColor);
	}

}

void teleHUDPlugin::RenderScreenAfterOverlays(const ScreenInfoV01 &info)
{
	return;
}

void teleHUDPlugin::RenderScreenBeforeOverlays(const ScreenInfoV01 &info)
{

	/* If we're not in realtime, not in green flag, etc...
	there's no need to display the Delta Best time */
	if (! NeedToDisplay())
		return;

	/* Can't draw without a font object */
	if (g_Font == NULL)
		return;

	DrawHUD(info);
}

/* Simple style: negative delta = green, positive delta = red */
D3DCOLOR teleHUDPlugin::TextColor(double delta)
{
	D3DCOLOR text_color = 0xE0000000;      /* Alpha (transparency) value */
	bool is_negative = delta < 0;
	double cutoff_val = 0.10;
	double abs_val = abs(delta);

	text_color |= is_negative
		? (COLOR_INTENSITY << 8)
		: (COLOR_INTENSITY << 16);

	return text_color;
}

D3DCOLOR teleHUDPlugin::BarColor(double delta, double delta_diff)
{
	static const D3DCOLOR ALPHA = 0xE0000000;
	bool is_gaining = delta_diff > 0;
	D3DCOLOR bar_color = ALPHA;
	bar_color |= is_gaining ? (COLOR_INTENSITY << 16) : (COLOR_INTENSITY << 8);

	double abs_val = abs(delta_diff);
	double cutoff_val = 0.02;

	if (abs_val <= cutoff_val) {
		unsigned int col_val = int(COLOR_INTENSITY * (1 / cutoff_val) * (cutoff_val - abs_val));
		if (is_gaining)
			bar_color |= (col_val << 8) + col_val;
		else
			bar_color |= (col_val << 16) + col_val;
	}

	return bar_color;
}

void teleHUDPlugin::PreReset(const ScreenInfoV01 &info)
{
	if (g_Font)
		g_Font->OnLostDevice();
	if (bar)
		bar->OnLostDevice();
}

void teleHUDPlugin::PostReset(const ScreenInfoV01 &info)
{
	if (g_Font)
		g_Font->OnResetDevice();
	if (bar)
		bar->OnResetDevice();
}

void teleHUDPlugin::LoadConfig(struct PluginConfig &config, const char *ini_file)
{

	// [Bar] section
	config.bar_left = GetPrivateProfileInt("Bar", "Left", 0, ini_file);
	config.bar_top = GetPrivateProfileInt("Bar", "Top", DEFAULT_BAR_TOP, ini_file);
	config.bar_width = GetPrivateProfileInt("Bar", "Width", DEFAULT_BAR_WIDTH, ini_file);
	config.bar_height = GetPrivateProfileInt("Bar", "Height", DEFAULT_BAR_HEIGHT, ini_file);
	config.bar_gutter = GetPrivateProfileInt("Bar", "Gutter", DEFAULT_BAR_TIME_GUTTER, ini_file);
	config.bar_enabled = GetPrivateProfileInt("Bar", "Enabled", 1, ini_file) == 1 ? true : false;

	// [Time] section
	config.time_top = GetPrivateProfileInt("Time", "Top", 0, ini_file);
	config.time_width = GetPrivateProfileInt("Time", "Width", DEFAULT_TIME_WIDTH, ini_file);
	config.time_height = GetPrivateProfileInt("Time", "Height", DEFAULT_TIME_HEIGHT, ini_file);
	config.time_font_size = GetPrivateProfileInt("Time", "FontSize", DEFAULT_FONT_SIZE, ini_file);
	config.time_enabled = GetPrivateProfileInt("Time", "Enabled", 1, ini_file) == 1 ? true : false;
	config.hires_updates = GetPrivateProfileInt("Time", "HiresUpdates", DEFAULT_HIRES_UPDATES, ini_file) == 1 ? true : false;
	GetPrivateProfileString("Time", "FontName", DEFAULT_FONT_NAME, config.time_font_name, FONT_NAME_MAXLEN, ini_file);

	// [Keyboard] section
	config.keyboard_magic = GetPrivateProfileInt("Keyboard", "MagicKey", DEFAULT_MAGIC_KEY, ini_file);

}
