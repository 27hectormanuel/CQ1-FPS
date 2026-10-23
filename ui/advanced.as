// SPDX-License-Identifier: CC0-1.0
// Added to frame 1 of the game's pause.gfx (Help & Options > Settings > Advanced) by the
// library (cq1fps.so): three more checkboxes, "60 FPS", "120 FPS" and "Show FPS", copies of the page's own
// "Use Recommended Settings" checkbox. They call Lua functions the library registers
// (cq64.GetSim60 / SetSim60, GetSim120 / SetSim120, GetShowFPS / SetShowFPS); without them they
// aren't added. The two rates exclude each other: checking one unchecks the other.
var cq64_initOptions2 = initOptions2;
initOptions2 = function()
{
   cq64_initOptions2();
   cq64_addAdvanced();
};
function cq64_refresh()
{
   cq64_hf_mc.init(_global.RunGameCommand("cq64.GetSim60()") == 1);
   cq64_hf2_mc.init(_global.RunGameCommand("cq64.GetSim120()") == 1);
   cq64_fps_mc.init(_global.RunGameCommand("cq64.GetShowFPS()") == 1);
}
function cq64_addCheckbox(name, depth, label, getter, setter)
{
   var c = _root[name];
   if(c == undefined)
   {
      c = checkbox_mc.duplicateMovieClip(name,depth);
   }
   // The copy's text fields are as wide as the original's hint line; shrink them to the text so
   // the checkbox's click area stays clear of its neighbours (the Done button).
   c.label1.text = label;
   c.label1._width = c.label1.textWidth + 8;
   c.label2.text = "";
   c.label2._width = 1;
   c.label2._visible = false;
   c.naturalWidth = c.label1._x + c.label1._width;
   c.init(_global.RunGameCommand(getter) == 1);
   c.updated = function(checked)
   {
      // only a press on the Advanced page itself (the game also sends the movie input while it
      // is not showing)
      if(_global.g_Navigator.currentPage != "options2")
      {
         this.init(_global.RunGameCommand(getter) == 1);
         return undefined;
      }
      _global.RunGameCommand(setter + "(" + (!checked ? 0 : 1) + ")");
      cq64_refresh();
   };
   // the page's frames come and go; these copies are not part of them
   c.onEnterFrame = function()
   {
      if(checkbox_mc == undefined)
      {
         this.removeMovieClip();
      }
   };
   return c;
}
function cq64_link()
{
   var last = !checkbox_mc.checked ? db6 : checkbox_mc;
   last.rightControl = cq64_hf_mc;
   cq64_hf_mc.leftControl = last;
   cq64_hf_mc.rightControl = cq64_hf2_mc;
   cq64_hf2_mc.leftControl = cq64_hf_mc;
   cq64_hf2_mc.rightControl = cq64_fps_mc;
   cq64_fps_mc.leftControl = cq64_hf2_mc;
   cq64_fps_mc.rightControl = button_done_mc;
   button_done_mc.leftControl = cq64_fps_mc;
}
function cq64_addAdvanced()
{
   if(_global.RunGameCommand("cq64 ~= nil and 1 or 0") != 1)
   {
      return undefined;
   }
   var a = cq64_addCheckbox("cq64_hf_mc",16001,"60 FPS","cq64.GetSim60()","cq64.SetSim60");
   var b = cq64_addCheckbox("cq64_hf2_mc",16003,"120 FPS","cq64.GetSim120()","cq64.SetSim120");
   var c = cq64_addCheckbox("cq64_fps_mc",16002,"Show FPS","cq64.GetShowFPS()","cq64.SetShowFPS");
   // In the empty space left of Done, lined up under the "Use Recommended Settings" checkbox,
   // side by side, scaled to fit before Done, and vertically centred on Done (by the label, as
   // the copies keep an invisible hint line under it).
   var gap = 24;
   var left = checkbox_mc._x;
   var room = button_done_mc._x - 16 - left;
   var total = a.naturalWidth + gap + b.naturalWidth + gap + c.naturalWidth;
   var scale = 100;
   if(total > room)
   {
      scale = Math.floor(100 * room / total);
   }
   a._xscale = a._yscale = b._xscale = b._yscale = c._xscale = c._yscale = scale;
   a._x = left;
   b._x = left + (a.naturalWidth + gap) * scale / 100;
   c._x = left + (a.naturalWidth + gap + b.naturalWidth + gap) * scale / 100;
   var mid = (a.label1._y + a.label1._height / 2) * scale / 100;
   a._y = b._y = c._y = button_done_mc._y + button_done_mc._height / 2 - mid;
   var updated = checkbox_mc.updated;
   checkbox_mc.updated = function(checked)
   {
      updated(checked);
      cq64_link();
   };
   cq64_link();
}
