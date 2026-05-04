/* ES40 emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 *
 * WWW    : http://es40.org
 * E-mail : camiel@camicom.com
 *
 *  This file is based upon Bochs.
 *
 *  Copyright (C) 2002  MandrakeSoft S.A.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

 /**
  * \file
  * Contains the code for the bx_sdl_gui_c class used for interfacing with
  * SDL.
  *
  * $Id$
  *
  * X-1.18       Martin Borgman                                  10-APR-2008
  *	    Handle SDL support on OS X through OS_X/SDLMain.m.
  *
  * X-1.15       Camiel Vanderhoeven                             29-FEB-2008
  *      Comments
  *
  * X-1.14       Camiel Vanderhoeven                             12-FEB-2008
  *      Moved keyboard code into it's own class (CKeyboard)
  *
  * X-1.13       Camiel Vanderhoeven                             22-JAN-2008
  *      Minor cleanups.
  *
  * X-1.12       Fang Zhe                                        05-JAN-2008
  *      Last patch was applied incompletely.
  *
  * X-1.11       Fang Zhe                                        04-JAN-2008
  *      Improved compatibility with Apple OS X; keyboard works now.
  *
  * X-1.10       Fang Zhe                                        03-JAN-2008
  *      Compatibility with Apple OS X.
  *
  * X-1.9        Camiel Vanderhoeven                             02-JAN-2008
  *      Comments.
  *
  * X-1.4        Camiel Vanderhoeven                             10-DEC-2007
  *      Use Configurator.
  *
  * X-1.3        Camiel Vanderhoeven                             7-DEC-2007
  *      Made keyboard messages conditional.
  *
  * X-1.2        Camiel Vanderhoeven                             7-DEC-2007
  *      Code cleanup.
  *
  * X-1.1        Camiel Vanderhoeven                             6-DEC-2007
  *      Initial version for ES40 emulator.
  *
  **/
#include "../StdAfx.h"

#if defined(HAVE_SDL)
#include "gui.h"
#include "keymap.h"
#include "../VGA.h"
#include "../System.h"

  //#include "../AliM1543C.h"
#include "../Keyboard.h"
#include "../Configurator.h"

#define _MULTI_THREAD

// Define BX_PLUGGABLE in files that can be compiled into plugins.  For
// platforms that require a special tag on exported symbols, BX_PLUGGABLE
// is used to know when we are exporting symbols and when we are importing.
#define BX_PLUGGABLE

#include <stdlib.h>
#include <string.h>
#include <vector>
#include <SDL3/SDL.h>

#include "sdl_fonts.h"

/**
 * \brief GUI implementation using SDL3.
 **/
class bx_sdl_gui_c : public bx_gui_c
{
public:
	bx_sdl_gui_c(CConfigurator* cfg);
	virtual void    specific_init(unsigned x_tilesize, unsigned y_tilesize) override;
	virtual void    text_update(u8* old_text, u8* new_text, unsigned long cursor_x, unsigned long cursor_y, bx_vga_tminfo_t tm_info, unsigned rows) override {}
	virtual void    graphics_tile_update(u8* snapshot, unsigned x, unsigned y) override;
	virtual void    handle_events(void) override;
	virtual void    flush(void) override;
	virtual void    clear_screen(void) override;
	virtual bool    palette_change(unsigned index, unsigned red, unsigned green, unsigned blue) override;
	virtual void    dimension_update(unsigned x, unsigned y, unsigned fheight = 0, unsigned fwidth = 0, unsigned bpp = 8) override;
	virtual void    mouse_enabled_changed_specific(bool val) override;
	virtual void    exit(void) override;
	virtual			bx_svga_tileinfo_t* graphics_tile_info(bx_svga_tileinfo_t* info) override;
	virtual			u8* graphics_tile_get(unsigned x, unsigned y, unsigned* w, unsigned* h) override;
	virtual void    graphics_tile_update_in_place(unsigned x, unsigned y, unsigned w, unsigned h) override;
	void			graphics_frame_update(const u32* pixels, unsigned w, unsigned h) override;
private:
	CConfigurator* myCfg;
};

// declare one instance of the gui object and call macro to insert the
// plugin code
static bx_sdl_gui_c* theGui = NULL;
IMPLEMENT_GUI_PLUGIN_CODE(sdl)
static unsigned     prev_cursor_x = 0;
static unsigned     prev_cursor_y = 0;
static u32          convertStringToSDLKey(const char* string);

static SDL_Window*   sdl_window = NULL;
static SDL_Renderer* sdl_renderer = NULL;
static SDL_Texture*  sdl_texture = NULL;

extern unsigned res_x, res_y;   // defined later in this file

// Toolbar strip across the top of the main window: machine status label
// and Start / Stop buttons. The VGA framebuffer is rendered below it with
// aspect preservation.
static const int TOOLBAR_H        = 40;
static const int TOOLBAR_FONT_SC  = 1;     // 8x16 glyph scale
static const int TOOLBAR_BTN_W    = 80;
static const int TOOLBAR_BTN_H    = 30;
static const int TOOLBAR_BTN_GAP  = 8;
static const int TOOLBAR_BTN_MARG = 10;    // right-edge margin
static const int TOOLBAR_DEFAULT_VGA_W = 640;
static const int TOOLBAR_DEFAULT_VGA_H = 480;

enum HitTarget { HIT_NONE, HIT_START, HIT_STOP };

static HitTarget tb_hover = HIT_NONE;
static bool      tb_dirty = true;
static SDL_FRect tb_rect_start{};
static SDL_FRect tb_rect_stop{};

static void tb_recalc_rects()
{
	if (!sdl_window) return;
	int w = 0, h = 0;
	SDL_GetWindowSize(sdl_window, &w, &h);
	const float bw = (float)TOOLBAR_BTN_W;
	const float bh = (float)TOOLBAR_BTN_H;
	const float by = (float)((TOOLBAR_H - TOOLBAR_BTN_H) / 2);
	const float right = (float)(w - TOOLBAR_BTN_MARG);
	tb_rect_stop  = SDL_FRect{ right - bw,                                by, bw, bh };
	tb_rect_start = SDL_FRect{ right - bw * 2.0f - TOOLBAR_BTN_GAP,       by, bw, bh };
}

static HitTarget tb_hit_test(int x, int y)
{
	auto inside = [&](const SDL_FRect& r) {
		return (float)x >= r.x && (float)x < r.x + r.w
			&& (float)y >= r.y && (float)y < r.y + r.h;
	};
	if (y >= TOOLBAR_H) return HIT_NONE;
	if (inside(tb_rect_start)) return HIT_START;
	if (inside(tb_rect_stop))  return HIT_STOP;
	return HIT_NONE;
}

static void tb_draw_char(int px, int py, int scale, char c, SDL_Color col)
{
	const unsigned char* glyph = sdl_font8x16[(unsigned char)c];
	SDL_SetRenderDrawColor(sdl_renderer, col.r, col.g, col.b, col.a);
	for (int row = 0; row < 16; ++row) {
		unsigned char bits = glyph[row];
		for (int colx = 0; colx < 8; ++colx) {
			if (bits & (0x80u >> colx)) {
				SDL_FRect r{ (float)(px + colx * scale),
				             (float)(py + row * scale),
				             (float)scale, (float)scale };
				SDL_RenderFillRect(sdl_renderer, &r);
			}
		}
	}
}

static int tb_text_width_px(int scale, const char* s)
{
	int n = 0; while (s[n]) ++n;
	return n * 8 * scale;
}

static void tb_draw_text(int px, int py, int scale, const char* s, SDL_Color col)
{
	for (int i = 0; s[i]; ++i)
		tb_draw_char(px + i * 8 * scale, py, scale, s[i], col);
}

static void tb_draw_button(const SDL_FRect& r, const char* label,
                           bool enabled, bool hovered)
{
	const SDL_Color face       = enabled ? (hovered ? SDL_Color{110,110,110,255}
	                                                : SDL_Color{ 80, 80, 80,255})
	                                     : SDL_Color{ 50, 50, 50,255};
	const SDL_Color border     = enabled ? SDL_Color{200,200,200,255}
	                                     : SDL_Color{120,120,120,255};
	const SDL_Color text       = enabled ? SDL_Color{240,240,240,255}
	                                     : SDL_Color{130,130,130,255};
	SDL_SetRenderDrawColor(sdl_renderer, face.r, face.g, face.b, face.a);
	SDL_RenderFillRect(sdl_renderer, &r);
	SDL_SetRenderDrawColor(sdl_renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(sdl_renderer, &r);

	const int tw = tb_text_width_px(TOOLBAR_FONT_SC, label);
	const int th = 16 * TOOLBAR_FONT_SC;
	const int tx = (int)(r.x + (r.w - (float)tw) * 0.5f);
	const int ty = (int)(r.y + (r.h - (float)th) * 0.5f);
	tb_draw_text(tx, ty, TOOLBAR_FONT_SC, label, text);
}

// Draws the toolbar strip in window-pixel coordinates.
static void tb_draw()
{
	if (!sdl_renderer || !sdl_window) return;

	int w = 0, h = 0;
	SDL_GetWindowSize(sdl_window, &w, &h);

	// Background bar
	SDL_FRect bar{ 0.f, 0.f, (float)w, (float)TOOLBAR_H };
	SDL_SetRenderDrawColor(sdl_renderer, 32, 32, 32, 255);
	SDL_RenderFillRect(sdl_renderer, &bar);
	// Bottom border line
	SDL_SetRenderDrawColor(sdl_renderer, 80, 80, 80, 255);
	SDL_FRect line{ 0.f, (float)(TOOLBAR_H - 1), (float)w, 1.f };
	SDL_RenderFillRect(sdl_renderer, &line);

	const bool running = theSystem ? theSystem->IsMachineRunning() : false;

	// Status text on the left.
	const char* prefix = "Machine: ";
	const char* state  = running ? "Running" : "Stopped";
	const SDL_Color stateCol = running ? SDL_Color{ 90,220,110,255}
	                                   : SDL_Color{230,110,110,255};
	const SDL_Color label    = SDL_Color{220,220,220,255};
	const int textY = (TOOLBAR_H - 16 * TOOLBAR_FONT_SC) / 2;
	tb_draw_text(10, textY, TOOLBAR_FONT_SC, prefix, label);
	tb_draw_text(10 + tb_text_width_px(TOOLBAR_FONT_SC, prefix),
	             textY, TOOLBAR_FONT_SC, state, stateCol);

	// Buttons. Disabled until theSystem exists; afterwards Start is enabled
	// only while stopped, Stop only while running.
	const bool clickable = (theSystem != nullptr);
	tb_recalc_rects();
	tb_draw_button(tb_rect_start, "Start",
	               clickable && !running,
	               tb_hover == HIT_START && clickable && !running);
	tb_draw_button(tb_rect_stop,  "Stop",
	               clickable && running,
	               tb_hover == HIT_STOP  && clickable && running);
}

// VGA blit rect: full window minus the toolbar, aspect preserved.
static void tb_compute_vga_rect(SDL_FRect* out)
{
	int w = 0, h = 0;
	SDL_GetWindowSize(sdl_window, &w, &h);
	int avail_h = h - TOOLBAR_H;
	if (avail_h < 1) avail_h = 1;

	float ax = (res_x ? (float)res_x : (float)TOOLBAR_DEFAULT_VGA_W);
	float ay = (res_y ? (float)res_y : (float)TOOLBAR_DEFAULT_VGA_H);
	const float aspect = ax / ay;

	int vga_w, vga_h;
	if ((float)avail_h * aspect <= (float)w) {
		vga_h = avail_h;
		vga_w = (int)((float)vga_h * aspect);
	} else {
		vga_w = w;
		vga_h = (int)((float)vga_w / aspect);
	}
	out->x = (float)((w - vga_w) / 2);
	out->y = (float)(TOOLBAR_H + (avail_h - vga_h) / 2);
	out->w = (float)vga_w;
	out->h = (float)vga_h;
}

// Render VGA texture (only when emulation is live, so a stale last-frame
// doesn't show through after Stop) plus the toolbar, then present.
static void tb_present()
{
	if (!sdl_renderer) return;
	SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
	SDL_RenderClear(sdl_renderer);
	if (sdl_texture && theSystem && theSystem->IsMachineRunning()) {
		SDL_FRect dst{};
		tb_compute_vga_rect(&dst);
		SDL_RenderTexture(sdl_renderer, sdl_texture, NULL, &dst);
	}
	tb_draw();
	SDL_RenderPresent(sdl_renderer);
}


SDL_Event           sdl_event;
int                 sdl_grab = 0;
unsigned            res_x = 0, res_y = 0;
unsigned            half_res_x, half_res_y;
u8                  old_mousebuttons = 0, new_mousebuttons = 0;
int                 old_mousex = 0, new_mousex = 0;
int                 old_mousey = 0, new_mousey = 0;
static int          sdl_mouse_button_state = 0;
static bool         sdl_swallow_keys = false;
static bool         sdl_swallow_end_release = false;
static const char*  sdl_title = "ES40 Emulator - Ctrl+Alt+End sends Ctrl+Alt+Del";
static const char*  sdl_title_grabbed = "ES40 Emulator - Ctrl+F10 releases mouse - Ctrl+Alt+End sends Ctrl+Alt+Del";

// SDL3 on Windows binds a window's message queue to the thread that created
// it, so all SDL state lives on a dedicated thread spawned by sdl_early_init;
// other threads communicate via the pending block below. The thread_local
// flag lets handle_events() no-op when S3 calls it from its own pump tick.
static thread_local bool      tls_is_sdl_thread = false;

// Set when the user closes the window. Polled by Run() / SingleStep on the
// main thread, which then throws FAILURE(Graceful) so AlphaSim's catch path
// can run normal shutdown — a FAILURE thrown from the SDL thread itself
// would just be swallowed by CThread's exception wrapper.
std::atomic<bool>             sdl_quit_requested{ false };

static std::mutex             sdl_pending_mtx;
static bool                   sdl_pending_frame = false;
static std::vector<u32>       sdl_pending_pixels;
static unsigned               sdl_pending_frame_w = 0;
static unsigned               sdl_pending_frame_h = 0;

static bool                   sdl_pending_dim = false;
static unsigned               sdl_pending_dim_w = 0;
static unsigned               sdl_pending_dim_h = 0;

static bool                   sdl_pending_mouse = false;
static bool                   sdl_pending_mouse_grab = false;

static bool                   sdl_pending_clear = false;

static unsigned               sdl_texture_w = 0;
static unsigned               sdl_texture_h = 0;

static std::atomic<bool>      sdl_thread_ready{ false };
static std::atomic<bool>      sdl_thread_stop{ false };

class SDLThreadRunner : public CRunnable
{
public:
	void run() override
	{
		tls_is_sdl_thread = true;
		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			FAILURE(SDL, "Unable to initialize SDL3 video subsystem");
		}

		const int w = TOOLBAR_DEFAULT_VGA_W;
		const int h = TOOLBAR_H + TOOLBAR_DEFAULT_VGA_H;

		sdl_window = SDL_CreateWindow(sdl_title, w, h, SDL_WINDOW_RESIZABLE);
		if (!sdl_window)
		{
			FAILURE_3(SDL, "Unable to create SDL3 window: %ix%i: %s\n",
				w, h, SDL_GetError());
		}

		sdl_renderer = SDL_CreateRenderer(sdl_window, NULL);
		if (!sdl_renderer)
		{
			FAILURE_3(SDL, "Unable to create SDL3 renderer: %ix%i: %s\n",
				w, h, SDL_GetError());
		}

		tb_recalc_rects();
		tb_dirty = true;
		tb_present();

		sdl_thread_ready.store(true, std::memory_order_release);

		while (!sdl_thread_stop.load(std::memory_order_acquire))
		{
			// Drain events. theGui might not exist yet during pre-init —
			// just pump so the window stays responsive in that case.
			if (theGui) theGui->handle_events();
			else        SDL_PumpEvents();

			// Apply pending requests posted by other threads.
			bool need_redraw = tb_dirty;
			{
				std::lock_guard<std::mutex> lk(sdl_pending_mtx);

				if (sdl_pending_dim)
				{
					if (sdl_texture)
					{
						SDL_DestroyTexture(sdl_texture);
						sdl_texture = NULL;
					}
					sdl_texture = SDL_CreateTexture(sdl_renderer,
						SDL_PIXELFORMAT_ARGB8888,
						SDL_TEXTUREACCESS_STREAMING,
						(int)sdl_pending_dim_w, (int)sdl_pending_dim_h);
					if (sdl_texture)
					{
						SDL_SetTextureScaleMode(sdl_texture, SDL_SCALEMODE_NEAREST);
						sdl_texture_w = sdl_pending_dim_w;
						sdl_texture_h = sdl_pending_dim_h;
						res_x = sdl_pending_dim_w;
						res_y = sdl_pending_dim_h;
						half_res_x = res_x / 2;
						half_res_y = res_y / 2;

						// Resize window to fit toolbar + new guest resolution.
						SDL_SetWindowSize(sdl_window,
							(int)sdl_pending_dim_w,
							(int)sdl_pending_dim_h + TOOLBAR_H);
						tb_recalc_rects();
					}
					sdl_pending_dim = false;
					need_redraw = true;
				}

				if (sdl_pending_frame && sdl_texture
					&& sdl_pending_frame_w == sdl_texture_w
					&& sdl_pending_frame_h == sdl_texture_h)
				{
					SDL_UpdateTexture(sdl_texture, NULL,
						sdl_pending_pixels.data(),
						(int)(sdl_pending_frame_w * sizeof(u32)));
					need_redraw = true;
				}
				sdl_pending_frame = false;

				if (sdl_pending_mouse)
				{
					if (sdl_pending_mouse_grab)
					{
						SDL_HideCursor();
						SDL_SetWindowRelativeMouseMode(sdl_window, true);
						SDL_SetWindowTitle(sdl_window, sdl_title_grabbed);
					}
					else
					{
						SDL_ShowCursor();
						SDL_SetWindowRelativeMouseMode(sdl_window, false);
						SDL_SetWindowTitle(sdl_window, sdl_title);
					}
					sdl_grab = sdl_pending_mouse_grab ? 1 : 0;
					sdl_pending_mouse = false;
				}

				if (sdl_pending_clear)
				{
					sdl_pending_clear = false;
					need_redraw = true;
				}
			}

			// Repaint when the machine state flips (toolbar color swap).
			{
				static bool prev_running = false;
				const bool now_running = theSystem ? theSystem->IsMachineRunning() : false;
				if (now_running != prev_running)
				{
					prev_running = now_running;
					need_redraw = true;
				}
			}

			if (need_redraw)
			{
				tb_present();
				tb_dirty = false;
			}

			SDL_Delay(16);
		}

		// Teardown on the same thread that created these.
		if (sdl_texture)  { SDL_DestroyTexture(sdl_texture);   sdl_texture = NULL; }
		if (sdl_renderer) { SDL_DestroyRenderer(sdl_renderer); sdl_renderer = NULL; }
		if (sdl_window)   { SDL_DestroyWindow(sdl_window);     sdl_window = NULL; }
	}
};

static SDLThreadRunner sdl_runner;
static CThread*        sdl_thread = nullptr;

bx_sdl_gui_c::bx_sdl_gui_c(CConfigurator* cfg)
{
	myCfg = cfg;
	bx_keymap = new bx_keymap_c(cfg);
}

// Called from AlphaSim main() so the window appears immediately. Spawns
// the SDL thread and waits briefly for it to confirm window creation.
void sdl_early_init()
{
	if (sdl_thread) return;
	sdl_thread = new CThread("sdl");
	sdl_thread->start(sdl_runner);

	// Bound the wait so a hard SDL init failure doesn't deadlock startup.
	for (int spin = 0; spin < 200; ++spin)   // up to ~2 seconds
	{
		if (sdl_thread_ready.load(std::memory_order_acquire))
			break;
		CThread::sleep(10);
	}
}

void bx_sdl_gui_c::specific_init(unsigned x_tilesize, unsigned y_tilesize)
{
	// Window/SDL state lives on the SDL thread (created by sdl_early_init).
	if (!sdl_thread)
		sdl_early_init();

	// SDL3: key repeat is handled by the OS; no SDL_EnableKeyRepeat().

	// load keymap for sdl
	if (myCfg->get_bool_value("keyboard.use_mapping", false))
	{
		bx_keymap->loadKeymap(convertStringToSDLKey);
	}

	new_gfx_api = 1;
}

void bx_sdl_gui_c::graphics_frame_update(const u32* pixels, unsigned width, unsigned height)
{
	// Hand the pixels to the SDL thread for upload/present.
	std::lock_guard<std::mutex> lk(sdl_pending_mtx);
	sdl_pending_frame_w = width;
	sdl_pending_frame_h = height;
	sdl_pending_pixels.assign(pixels, pixels + (size_t)width * height);
	sdl_pending_frame = true;
}

void bx_sdl_gui_c::graphics_tile_update(u8* snapshot, unsigned x, unsigned y)
{
//
}

bx_svga_tileinfo_t* bx_sdl_gui_c::graphics_tile_info(bx_svga_tileinfo_t* info)
{
	return NULL;
}

u8* bx_sdl_gui_c::graphics_tile_get(unsigned x0, unsigned y0, unsigned* w,
	unsigned* h)
{
	return NULL;
}

void bx_sdl_gui_c::graphics_tile_update_in_place(unsigned x0, unsigned y0,
	unsigned w, unsigned h)
{
	//
}

static u32 sdl_sym_to_bx_key(SDL_Keycode sym)
{
	switch (sym)
	{
	case SDLK_BACKSPACE:    return BX_KEY_BACKSPACE;
	case SDLK_TAB:          return BX_KEY_TAB;
	case SDLK_RETURN:       return BX_KEY_ENTER;
	case SDLK_PAUSE:        return BX_KEY_PAUSE;
	case SDLK_ESCAPE:       return BX_KEY_ESC;
	case SDLK_SPACE:        return BX_KEY_SPACE;
	case SDLK_APOSTROPHE:   return BX_KEY_SINGLE_QUOTE;
	case SDLK_COMMA:        return BX_KEY_COMMA;
	case SDLK_MINUS:        return BX_KEY_MINUS;
	case SDLK_PERIOD:       return BX_KEY_PERIOD;
	case SDLK_SLASH:        return BX_KEY_SLASH;

	case SDLK_0:            return BX_KEY_0;
	case SDLK_1:            return BX_KEY_1;
	case SDLK_2:            return BX_KEY_2;
	case SDLK_3:            return BX_KEY_3;
	case SDLK_4:            return BX_KEY_4;
	case SDLK_5:            return BX_KEY_5;
	case SDLK_6:            return BX_KEY_6;
	case SDLK_7:            return BX_KEY_7;
	case SDLK_8:            return BX_KEY_8;
	case SDLK_9:            return BX_KEY_9;

	case SDLK_SEMICOLON:    return BX_KEY_SEMICOLON;
	case SDLK_EQUALS:       return BX_KEY_EQUALS;

	case SDLK_LEFTBRACKET:  return BX_KEY_LEFT_BRACKET;
	case SDLK_BACKSLASH:    return BX_KEY_BACKSLASH;
	case SDLK_RIGHTBRACKET: return BX_KEY_RIGHT_BRACKET;
	case SDLK_GRAVE:        return BX_KEY_GRAVE;

	case SDLK_A:            return BX_KEY_A;
	case SDLK_B:            return BX_KEY_B;
	case SDLK_C:            return BX_KEY_C;
	case SDLK_D:            return BX_KEY_D;
	case SDLK_E:            return BX_KEY_E;
	case SDLK_F:            return BX_KEY_F;
	case SDLK_G:            return BX_KEY_G;
	case SDLK_H:            return BX_KEY_H;
	case SDLK_I:            return BX_KEY_I;
	case SDLK_J:            return BX_KEY_J;
	case SDLK_K:            return BX_KEY_K;
	case SDLK_L:            return BX_KEY_L;
	case SDLK_M:            return BX_KEY_M;
	case SDLK_N:            return BX_KEY_N;
	case SDLK_O:            return BX_KEY_O;
	case SDLK_P:            return BX_KEY_P;
	case SDLK_Q:            return BX_KEY_Q;
	case SDLK_R:            return BX_KEY_R;
	case SDLK_S:            return BX_KEY_S;
	case SDLK_T:            return BX_KEY_T;
	case SDLK_U:            return BX_KEY_U;
	case SDLK_V:            return BX_KEY_V;
	case SDLK_W:            return BX_KEY_W;
	case SDLK_X:            return BX_KEY_X;
	case SDLK_Y:            return BX_KEY_Y;
	case SDLK_Z:            return BX_KEY_Z;

	case SDLK_DELETE:       return BX_KEY_DELETE;

		// Keypad
	case SDLK_KP_0:         return BX_KEY_KP_INSERT;
	case SDLK_KP_1:         return BX_KEY_KP_END;
	case SDLK_KP_2:         return BX_KEY_KP_DOWN;
	case SDLK_KP_3:         return BX_KEY_KP_PAGE_DOWN;
	case SDLK_KP_4:         return BX_KEY_KP_LEFT;
	case SDLK_KP_5:         return BX_KEY_KP_5;
	case SDLK_KP_6:         return BX_KEY_KP_RIGHT;
	case SDLK_KP_7:         return BX_KEY_KP_HOME;
	case SDLK_KP_8:         return BX_KEY_KP_UP;
	case SDLK_KP_9:         return BX_KEY_KP_PAGE_UP;
	case SDLK_KP_PERIOD:    return BX_KEY_KP_DELETE;
	case SDLK_KP_DIVIDE:    return BX_KEY_KP_DIVIDE;
	case SDLK_KP_MULTIPLY:  return BX_KEY_KP_MULTIPLY;
	case SDLK_KP_MINUS:     return BX_KEY_KP_SUBTRACT;
	case SDLK_KP_PLUS:      return BX_KEY_KP_ADD;
	case SDLK_KP_ENTER:     return BX_KEY_KP_ENTER;

		// Arrows + Home/End pad
	case SDLK_UP:           return BX_KEY_UP;
	case SDLK_DOWN:         return BX_KEY_DOWN;
	case SDLK_RIGHT:        return BX_KEY_RIGHT;
	case SDLK_LEFT:         return BX_KEY_LEFT;
	case SDLK_INSERT:       return BX_KEY_INSERT;
	case SDLK_HOME:         return BX_KEY_HOME;
	case SDLK_END:          return BX_KEY_END;
	case SDLK_PAGEUP:       return BX_KEY_PAGE_UP;
	case SDLK_PAGEDOWN:     return BX_KEY_PAGE_DOWN;

		// Function keys
	case SDLK_F1:           return BX_KEY_F1;
	case SDLK_F2:           return BX_KEY_F2;
	case SDLK_F3:           return BX_KEY_F3;
	case SDLK_F4:           return BX_KEY_F4;
	case SDLK_F5:           return BX_KEY_F5;
	case SDLK_F6:           return BX_KEY_F6;
	case SDLK_F7:           return BX_KEY_F7;
	case SDLK_F8:           return BX_KEY_F8;
	case SDLK_F9:           return BX_KEY_F9;
	case SDLK_F10:          return BX_KEY_F10;
	case SDLK_F11:          return BX_KEY_F11;
	case SDLK_F12:          return BX_KEY_F12;

		// Modifier keys
	case SDLK_NUMLOCKCLEAR: return BX_KEY_NUM_LOCK;
	case SDLK_CAPSLOCK:     return BX_KEY_CAPS_LOCK;
	case SDLK_SCROLLLOCK:   return BX_KEY_SCRL_LOCK;
	case SDLK_RSHIFT:       return BX_KEY_SHIFT_R;
	case SDLK_LSHIFT:       return BX_KEY_SHIFT_L;
	case SDLK_RCTRL:        return BX_KEY_CTRL_R;
	case SDLK_LCTRL:        return BX_KEY_CTRL_L;
	case SDLK_RALT:         return BX_KEY_ALT_R;
	case SDLK_LALT:         return BX_KEY_ALT_L;
	case SDLK_LGUI:         return BX_KEY_WIN_L;
	case SDLK_RGUI:         return BX_KEY_WIN_R;

		// Misc function keys
	case SDLK_PRINTSCREEN:  return BX_KEY_PRINT;
	case SDLK_MENU:         return BX_KEY_MENU;

	default:
		BX_ERROR(("sdl3 keycode 0x%x not mapped", (unsigned)sym));
		return BX_KEY_UNHANDLED;
	}
}

void bx_sdl_gui_c::handle_events(void)
{
	// Only drain on the SDL thread (S3 still calls this — no-op for it).
	if (!tls_is_sdl_thread)
		return;

	u32 key_event;

	while (SDL_PollEvent(&sdl_event))
	{
		switch (sdl_event.type)
		{
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			sdl_quit_requested.store(true, std::memory_order_release);
			break;

		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			tb_dirty = true;
			break;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			if (tb_hover != HIT_NONE) { tb_hover = HIT_NONE; tb_dirty = true; }
			break;

		case SDL_EVENT_MOUSE_MOTION:
			if (sdl_grab)
			{
				int dx = (int)sdl_event.motion.xrel;
				int dy = -(int)sdl_event.motion.yrel;

				if (dx != 0 || dy != 0)
				{
					theKeyboard->mouse_motion(dx, dy, 0, sdl_mouse_button_state);
				}
			}
			else
			{
				HitTarget h = tb_hit_test((int)sdl_event.motion.x,
				                          (int)sdl_event.motion.y);
				if (h != tb_hover) { tb_hover = h; tb_dirty = true; }
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			if (!sdl_grab)
			{
				if (sdl_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
					&& sdl_event.button.button == SDL_BUTTON_LEFT)
				{
					// Toolbar click: drive the controls. Otherwise it's a
					// click in the VGA area — grab the mouse like before.
					HitTarget h = tb_hit_test((int)sdl_event.button.x,
					                          (int)sdl_event.button.y);
					if (h != HIT_NONE)
					{
						if (theSystem && h == HIT_START) theSystem->RequestStart();
						else if (theSystem && h == HIT_STOP) theSystem->RequestStop();
						tb_dirty = true;
					}
					else if ((int)sdl_event.button.y >= TOOLBAR_H)
					{
						bx_gui->mouse_enabled_changed(true);
					}
				}
				break;
			}

			int bitmask = 0;
			switch (sdl_event.button.button)
			{
			case SDL_BUTTON_LEFT:   bitmask = 0x01; break;
			case SDL_BUTTON_RIGHT:  bitmask = 0x02; break;
			case SDL_BUTTON_MIDDLE: bitmask = 0x04; break;
			default: break;
			}

			if (sdl_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
				sdl_mouse_button_state |= bitmask;
			else
				sdl_mouse_button_state &= ~bitmask;

			theKeyboard->mouse_motion(0, 0, 0, sdl_mouse_button_state);
			break;
		}

		case SDL_EVENT_MOUSE_WHEEL: // disabled for now, isn't working properly yet.
/*			if (sdl_grab)
			{
				int dz = (int)sdl_event.wheel.y;  // positive = scroll up
				if (dz != 0)
				{
					theKeyboard->mouse_motion(0, 0, dz, sdl_mouse_button_state);
				}
			} */
			break;

		case SDL_EVENT_KEY_DOWN:
			if (sdl_event.key.key == SDLK_END &&
				(sdl_event.key.mod & SDL_KMOD_CTRL) &&
				(sdl_event.key.mod & SDL_KMOD_ALT))
			{
				theKeyboard->gen_scancode(BX_KEY_DELETE);
				theKeyboard->gen_scancode(BX_KEY_DELETE | BX_KEY_RELEASED);
				sdl_swallow_end_release = true;
				break;
			}

			// Ctrl+F10: toggle mouse capture
			if (sdl_event.key.key == SDLK_F10 && (sdl_event.key.mod & SDL_KMOD_CTRL))
			{
				theKeyboard->gen_scancode(BX_KEY_CTRL_L | BX_KEY_RELEASED);
				theKeyboard->gen_scancode(BX_KEY_CTRL_R | BX_KEY_RELEASED);

				bx_gui->mouse_enabled_changed(!sdl_grab);
				sdl_swallow_keys = true;  // eat subsequent releases
				break;
			}
			if (sdl_swallow_keys)
				break;  // swallow any key-down during toggle

			// Filter out ScrollLock (fullscreen toggle prev.) and invalid keys
			if (sdl_event.key.key == SDLK_SCROLLLOCK)
				break;

			// convert sym -> bochs code
			if (!myCfg->get_bool_value("keyboard.use_mapping", false))
			{
				key_event = sdl_sym_to_bx_key(sdl_event.key.key);
			}
			else
			{
				/* use mapping */
				BXKeyEntry* entry = bx_keymap->findHostKey(sdl_event.key.key);
				if (!entry)
				{
					BX_ERROR(("host key 0x%x not mapped!",
						(unsigned)sdl_event.key.key));
					break;
				}
				key_event = entry->baseKey;
			}

			if (key_event == BX_KEY_UNHANDLED)
				break;

			theKeyboard->gen_scancode(key_event);

			// Locks: generate immediate press+release pair
			if ((key_event == BX_KEY_NUM_LOCK) || (key_event == BX_KEY_CAPS_LOCK))
			{
				theKeyboard->gen_scancode(key_event | BX_KEY_RELEASED);
			}
			break;

		case SDL_EVENT_KEY_UP:
			if (sdl_event.key.key == SDLK_SCROLLLOCK)
				break;

			if (sdl_swallow_end_release && sdl_event.key.key == SDLK_END)
			{
				sdl_swallow_end_release = false;
				break;
			}

			if (sdl_swallow_keys)
			{
				// hanlde dealing with ctrl+f10 escape
				if (!(SDL_GetModState() & SDL_KMOD_CTRL))
					sdl_swallow_keys = false;
				break;
			}

			if (!myCfg->get_bool_value("keyboard.use_mapping", false))
			{
				key_event = sdl_sym_to_bx_key(sdl_event.key.key);
			}
			else
			{
				BXKeyEntry* entry = bx_keymap->findHostKey(sdl_event.key.key);
				if (!entry)
				{
					BX_ERROR(("host key 0x%x not mapped!",
						(unsigned)sdl_event.key.key));
					break;
				}
				key_event = entry->baseKey;
			}

			if (key_event == BX_KEY_UNHANDLED)
				break;

			if ((key_event == BX_KEY_NUM_LOCK) || (key_event == BX_KEY_CAPS_LOCK))
			{
				theKeyboard->gen_scancode(key_event);
			}

			theKeyboard->gen_scancode(key_event | BX_KEY_RELEASED);
			break;

		case SDL_EVENT_QUIT:
			sdl_quit_requested.store(true, std::memory_order_release);
			break;
		}
	}
}

/**
 * Flush any changes to sdl_screen to the actual window.
 **/
void bx_sdl_gui_c::flush(void)
{
	//
}

/**
 * Clear sdl_screen display, and flush it.
 **/
void bx_sdl_gui_c::clear_screen(void)
{
	std::lock_guard<std::mutex> lk(sdl_pending_mtx);
	sdl_pending_clear = true;
}

/**
 * Set palette-entry index to the desired value.
 *
 * The palette is used in text-mode and in 8bpp VGA mode.
 **/
bool bx_sdl_gui_c::palette_change(unsigned index, unsigned red, unsigned green,
	unsigned blue)
{
	return 1;
}

void bx_sdl_gui_c::dimension_update(unsigned x, unsigned y, unsigned fheight,
	unsigned fwidth, unsigned bpp)
{
	// Post the new dims; texture (re)creation happens on the SDL thread.
	std::lock_guard<std::mutex> lk(sdl_pending_mtx);
	if (sdl_texture_w == x && sdl_texture_h == y && !sdl_pending_dim)
		return;
	sdl_pending_dim_w = x;
	sdl_pending_dim_h = y;
	sdl_pending_dim = true;
}

void bx_sdl_gui_c::mouse_enabled_changed_specific(bool val)
{
	// Defer SDL APIs (cursor, relative mouse, title) to the SDL thread.
	// Flip sdl_grab now so a click later in the same iteration sees it.
	{
		std::lock_guard<std::mutex> lk(sdl_pending_mtx);
		sdl_pending_mouse_grab = val;
		sdl_pending_mouse = true;
	}
	sdl_grab = val ? 1 : 0;
}

void bx_sdl_gui_c::exit(void)
{
	// Signal the SDL thread to wind down; SDL teardown happens at the end
	// of its run() on the same thread that created the window.
	sdl_thread_stop.store(true, std::memory_order_release);
	if (sdl_thread) {
		sdl_thread->join();
		delete sdl_thread;
		sdl_thread = nullptr;
	}
}

/// key mapping for SDL
typedef struct
{
	const char* name;
	u32           value;
} keyTableEntry;

#define DEF_SDL_KEY(key) \
  {                      \
    #key, key            \
  },

keyTableEntry keytable[] = {
	// this include provides all the entries.
  #include "sdlkeys.h"
	// one final entry to mark the end
	{ NULL, 0}
};

// function to convert key names into SDLKey values.
// This first try will be horribly inefficient, but it only has
// to be done while loading a keymap.  Once the simulation starts,

// this function won't be called.
static u32 convertStringToSDLKey(const char* string)
{
	keyTableEntry* ptr;
	for (ptr = &keytable[0]; ptr->name != NULL; ptr++)
	{
		if (!strcmp(string, ptr->name))
			return ptr->value;
	}
	return 0;
}
#endif //defined(HAVE_SDL)
