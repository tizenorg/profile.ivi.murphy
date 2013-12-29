with_system_controller = true
with_amb = false
verbose = 0

m = murphy.get()

-- try loading the various logging plugins
m:try_load_plugin('systemd')
m:try_load_plugin('dlog')

-- load the console plugin
m:try_load_plugin('console')

m:try_load_plugin('console.disabled', 'webconsole', {
                  address = 'wsck:127.0.0.1:3000/murphy',
                  httpdir = '/usr/share/murphy/webconsole' });

-- load the dbus plugin
if m:plugin_exists('dbus') then
    m:load_plugin('dbus')
end

-- load the native resource plugin
if m:plugin_exists('resource-native') then
    m:load_plugin('resource-native')
    m:info("native resource plugin loaded")
else
    m:info("No native resource plugin found...")
end

-- load the dbus resource plugin
m:try_load_plugin('resource-dbus', {
    dbus_bus = "system",
    dbus_service = "org.Murphy",
    dbus_track = true,
    default_zone = "driver",
    default_class = "implicit"
})

-- load the domain control plugin
if m:plugin_exists('domain-control') then
    m:load_plugin('domain-control')
else
    m:info("No domain-control plugin found...")
end

-- load the AMB plugin
if m:plugin_exists('amb') then
    m:try_load_plugin('amb')

    if builtin.method.amb_initiate and
       builtin.method.amb_update
    then
        with_amb = true
    end
else
    m:info("No amb plugin found...")
end

-- load the ASM resource plugin
if m:plugin_exists('resource-asm') then
    m:try_load_plugin('resource-asm', {
        zone = "driver",
        share_mmplayer = "player:AVP,mandatory,exclusive,strict",
        ignored_argv0 = "WebProcess"
    })
else
    m:info("No audio session manager plugin found...")
end

if m:plugin_exists('ivi-resource-manager') then
    m:load_plugin('ivi-resource-manager')
    with_system_controller = false
end

-- define application classes
application_class {
    name     = "interrupt",
    priority = 99,
    modal    = true ,
    share    = false,
    order    = "fifo"
}

application_class {
    name     = "emergency",
    priority = 80,
    modal    = false,
    share    = false,
    order    = "fifo"
}
application_class {
    name     = "alert",
    priority = 51,
    modal    = false,
    share    = false,
    order    = "fifo"
}

application_class {
    name     = "navigator",
    priority = 50,
    modal    = false,
    share    = true,
    order    = "fifo"
}

application_class {
    name     = "phone",
    priority = 6 ,
    modal    = false,
    share    = true ,
    order    = "lifo"
}
application_class {
    name     = "camera",
    priority = 5,
    modal    = false,
    share    = false,
    order    = "lifo"
}

application_class { name="event"    , priority=4 , modal=false, share=true , order="fifo" }
application_class { name="game"     , priority=3 , modal=false, share=false, order="lifo" }
--# doesn't need to be created here, ivi-resource-manager creates it if loaded
--#application_class { name="basic"    , priority=2 , modal=false, share=false, order="lifo" }
application_class { name="player"   , priority=1 , modal=false, share=true , order="lifo" }
application_class { name="implicit" , priority=0 , modal=false, share=false, order="lifo" }

-- define zone attributes
zone.attributes {
    type = {mdb.string, "common", "rw"},
    location = {mdb.string, "anywhere", "rw"}
}

-- define zones
zone {
     name = "driver",
     attributes = {
         type = "common",
         location = "front-left"
     }
}

zone {
     name = "passanger1",
     attributes = {
         type = "private",
         location = "front-right"
     }
}

zone {
     name = "passanger2",
     attributes = {
         type = "private",
         location = "back-left"
     }
}

zone {
     name = "passanger3",
     attributes = {
         type = "private",
         location = "back-right"
     }
}

zone {
     name = "passanger4",
     attributes = {
         type = "private",
         location = "back-left"
     }
}


-- define resource classes
if not m:plugin_exists('ivi-resource-manager') then
   resource.class {
        name = "audio_playback",
        shareable = true,
        attributes = {
            role = { mdb.string, "music", "rw" },
            pid = { mdb.string, "<unknown>", "rw" },
            policy = { mdb.string, "relaxed", "rw" }
        }
   }
end

resource.class {
     name = "audio_recording",
     shareable = true,
     attributes = {
         role   = { mdb.string, "music"    , "rw" },
         pid    = { mdb.string, "<unknown>", "rw" },
         policy = { mdb.string, "relaxed"  , "rw" }
     }
}

resource.class {
     name = "video_playback",
     shareable = false,
}

resource.class {
     name = "video_recording",
     shareable = false
}

if not m:plugin_exists('ivi-resource-manager') then
resource.method.veto = {
    function(zone, rset, grant, owners)
	rset_priority = application_class[rset.application_class].priority

	owner_id = owners.audio_playback.resource_set
	rset_id = rset.id

        if (rset_priority >= 50 and owner_id ~= rset_id) then
            print("*** resource-set "..rset_id.." - veto")
            return false
        end

        return true
    end
}
end

-- test for creating selections
mdb.select {
           name = "audio_owner",
           table = "audio_playback_owner",
           columns = {"application_class"},
           condition = "zone_name = 'driver'"
}

mdb.select {
           name = "vehicle_speed",
           table = "amb_vehicle_speed",
           columns = {"value"},
           condition = "key = 'VehicleSpeed'"
}

element.lua {
   name    = "speed2volume",
   inputs  = { speed = mdb.select.vehicle_speed, param = 9 },
   outputs = {  mdb.table { name = "speedvol",
			    index = {"zone", "device"},
			    columns = {{"zone", mdb.string, 16},
				       {"device", mdb.string, 16},
				       {"value", mdb.floating}},
                            create = true
			   }
	     },
   oldvolume = 0.0,
   update  = function(self)
                speed = self.inputs.speed.single_value
                if (speed) then
                    volume = (speed - 144.0) / 7.0
                else
                    volume = 0.0
                end
                diff = volume - self.oldvolume
                if (diff*diff > self.inputs.param) then
                    print("*** element "..self.name.." update "..volume)
                    self.oldvolume = volume
		    mdb.table.speedvol:replace({zone = "driver", device = "speakers", value = volume})
		end
	     end
}

-- Night mode processing chain

mdb.select {
    name = "exterior_brightness",
    table = "amb_exterior_brightness",
    columns = { "value" },
    condition = "key = 'ExteriorBrightness'"
}

element.lua {
    name    = "nightmode",
    inputs  = { brightness = mdb.select.exterior_brightness },
    oldmode = -1;
    outputs = {
        mdb.table {
            name = "amb_nightmode",
            index = { "id" },
            create = true,
            columns = {
                { "id", mdb.unsigned },
                { "night_mode", mdb.unsigned }
            }
        }
    },
    update = function(self)
        -- This is a trivial function to calculate night mode. Later, we will
        -- need a better threshold value and hysteresis to prevent oscillation.

        brightness = self.inputs.brightness.single_value

        if not brightness then
            return
        end

        print("*** element "..self.name.." update brightness: "..brightness)

        if brightness > 300 then
            mode = 0
        else
            mode = 1
        end

        print("*** resulting mode: ".. mode)

        if not (mode == self.oldmode) then
            mdb.table.amb_nightmode:replace({ id = 0, night_mode = mode })
        end

        self.oldmode = mode
    end
}

mdb.select {
    name = "select_night_mode",
    table = "amb_nightmode",
    columns = { "night_mode" },
    condition = "id = 0"
}

if with_amb then
    sink.lua {
        name = "night_mode",
        inputs = { owner = mdb.select.select_night_mode },
        property = "NightMode",
        type = "b",
        initiate = builtin.method.amb_initiate,
        update = builtin.method.amb_update
    }
end


-- Driving mode processing chain


element.lua {
    name    = "drivingmode",
    inputs  = { speed = mdb.select.vehicle_speed },
    oldmode = -1;
    outputs = {
        mdb.table {
            name = "amb_drivingmode",
            index = { "id" },
            create = true,
            columns = {
                { "id", mdb.unsigned },
                { "driving_mode", mdb.unsigned }
            }
        }
    },
    update = function(self)

        speed = self.inputs.speed.single_value

        if not speed then
            return
        end

        if speed == 0 then
            mode = 0
        else
            mode = 1
        end

        if not (mode == self.oldmode) then
            mdb.table.amb_drivingmode:replace({ id = 0, driving_mode = mode })
        end

        self.oldmode = mode
    end
}

mdb.select {
    name = "select_driving_mode",
    table = "amb_drivingmode",
    columns = { "driving_mode" },
    condition = "id = 0"
}

if with_amb then
    sink.lua {
        name = "driving_mode",
        inputs = { owner = mdb.select.select_driving_mode },
        property = "DrivingMode",
        type = "u",
        initiate = builtin.method.amb_initiate,
        update = builtin.method.amb_update
    }
end

-- turn signals (left, right)

mdb.select {
    name = "winker",
    table = "amb_turn_signal",
    columns = { "value" },
    condition = "key = 'TurnSignal'"
}

-- regulation (on), use "select_driving_mode"

-- shift position (parking, reverse, other)

mdb.select {
    name = "gear_position",
    table = "amb_gear_position",
    columns = { "value" },
    condition = "key = 'GearPosition'"
}

-- cameras (back, front, left, right)

element.lua {
    name    = "camera_state",
    inputs  = { winker = mdb.select.winker, gear = mdb.select.gear_position },
    oldmode = -1;
    outputs = {
        mdb.table {
            name = "target_camera_state",
            index = { "id" },
            create = true,
            columns = {
                { "id", mdb.unsigned },
                { "front_camera", mdb.unsigned },
                { "back_camera", mdb.unsigned },
                { "right_camera", mdb.unsigned },
                { "left_camera", mdb.unsigned }
            }
        }
    },
    update = function(self)

        front_camera = 0
        back_camera = 0
        right_camera = 0
        left_camera = 0

        if self.inputs.gear == 128 then
            back_camera = 1
        elseif self.inputs.winker == 1 then
            right_camera = 1
        elseif self.inputs.winker == 2 then
            left_camera = 1
        end

        mdb.table.target_camera_state:replace({ id = 0, front_camera = front_camera, back_camera = back_camera, right_camera = right_camera, left_camera = left_camera })

    end
}

-- load the telephony plugin
m:try_load_plugin('telephony')


-- system controller test setup

if not with_system_controller or not m:plugin_exists('system-controller') then
   return
end

m:load_plugin('system-controller')

window_manager_operation_names = {
    [1] = "create",
    [2] = "destroy"
}

function window_manager_operation_name(oper)
    local name = window_manager_operation_names[oper]
    if name then return name end
    return "<unknown " .. tostring(oper) .. ">"
end

window_operation_names = {
    [1] = "create",
    [2] = "destroy",
    [3] = "name_change",
    [4] = "visible",
    [5] = "configure",
    [6] = "active"
}

function window_operation_name(oper)
    local name = window_operation_names[oper]
    if name then return name end
    return "<unknown " .. tostring(oper) .. ">"
end

layer_operation_names = {
    [1] = "create",
    [2] = "destroy",
    [3] = "visible"
}

function layer_operation_name(oper)
    local name = layer_operation_names[oper]
    if name then return name end
    return "<unknown " .. tostring(oper) .. ">"
end

command_names = {
    [0x00001] = "send_appid",
    [0x10001] = "create",
    [0x10002] = "destroy",
    [0x10003] = "show",
    [0x10004] = "hide",
    [0x10005] = "move",
    [0x10006] = "change_active",
    [0x10007] = "change_layer",
    [0x10008] = "change_attr",
    [0x10009] = "name",
    [0x10011] = "map_thumb",
    [0x10012] = "unmap_thumb",
    [0x10020] = "show layer",
    [0x10021] = "hide_layer",
    [0x10022] = "change_layer_attr",
    [0x20001] = "add_input",
    [0x20002] = "del_input",
    [0x20003] = "send_input",
    [0x40001] = "acquire_res",
    [0x40002] = "release_res",
    [0x40003] = "deprive_res",
    [0x40004] = "waiting_res",
    [0x40005] = "revert_res",
    [0x40011] = "create_res",
    [0x40012] = "destroy_res",
    [0x50001] = "set_region",
    [0x50002] = "unset_region",
    [0x60001] = "change_state"
}

function command_name(command)
    local name = command_names[command]
    if name then return name end
    return "<unknown " .. tostring(command) .. ">"
end

input_layer = {
   [3] = true, -- input
   [4] = true, -- touch
   [5] = true  -- cursor
}

resmgr = resource_manager {
  screen_event_handler = function(self, ev)
                             local event = ev.event
                             local surface = ev.surface

                             if event == "grant" then
                                 if verbose > 0 then
                                    print("*** make visible surface "..surface)
                                 end
                                 local a = animation({})
                                 local r = m:JSON({surface = surface,
                                                   visible = 1,
                                                   raise   = 1})
                                 wmgr:window_request(r,a,0)
                                 elseif event == "revoke" then
                                 if verbose > 0 then
                                    print("*** hide surface "..surface)
                                 end
                                 local a = animation({})
                                 local r = m:JSON({surface = ev.surface,
                                                   visible = 0})
                                 wmgr:window_request(r,a,0)
                             else
                                 if verbose > 0 then
                                    print("*** resource event: "..tostring(ev))
                                 end
                             end
                         end
}

resclnt = resource_client {}

wmgr = window_manager {
  geometry = function(self, w,h, v)
                  if type(v) == "function" then
                      return v(w,h)
                  end
                  return v
             end,

  application = function(self, appid)
                     if appid then
                         local app = application_lookup(appid)
                         if not app then
                             app = application_lookup("default")
                         end
                         return app
                     end
                     return { privileges = {screen="none", audio="none"} }
                end,

  outputs = { { name  = "Center",
                id    = 0,
                zone  = "driver",
                areas = { Status = {
                              id     = 0,
                              pos_x  = 0,
                              pos_y  = 0,
                              width  = function(w,h) return w end,
                              height = 64
                          },
                          Full = {
                              id     = 1,
                              pos_x  = 0,
                              pos_y  = 64,
                              width  = function(w,h) return w end,
                              height = function(w,h) return h-64-128 end
                          },
                          Upper = {
                              id     = 2,
                              pos_x  = 0,
                              pos_y  = 64,
                              width  = function(w,h) return w end,
                              height = function(w,h) return (h-64-128)/2 end
                          },
                          Lower = {
                              id     = 3,
                              pos_x  = 0,
                              pos_y  = function(w,h) return (h-64-128)/2+64 end,
                              width  = function(w,h) return w end,
                              height = function(w,h) return (h-64-128)/2 end
                          },
                          UpperLeft = {
                              id     = 4,
                              pos_x  = 0,
                              pos_y  = 64,
                              width  = function(w,h) return w/2 end,
                              height = function(w,h) return (h-64-128)/2 end
                          },
                          UpperRight = {
                              id     = 5,
                              pos_x  = function(w,h) return w/2 end,
                              pos_y  = 64,
                              width  = function(w,h) return w/2 end,
                              height = function(w,h) return (h-64-128)/2 end
                          },
                          LowerLeft = {
                              id     = 6,
                              pos_x  = 0,
                              pos_y  = function(w,h) return (h-64-128/2)+64 end,
                              width  = function(w,h) return w/2 end,
                              height = function(w,h) return (h-64-128)/2 end
                          },
                          LowerRight = {
                              id     = 7,
                              pos_x  = function(w,h) return w/2 end,
                              pos_y  = function(w,h) return (h-64-128/2)+64 end,
                              width  = function(w,h) return w/2 end,
                              height = function(w,h) return (h-64-128)/2 end
                          },
                          SysApp = {
                              id     = 8,
                              pos_x  = 0,
                              pos_y  = 64,
                              width  = function(w,h) return w end,
                              height = function(w,h) return h-64-128 end
                          },
                          ["SysApp.Left"] = {
                              id     = 9,
                              pos_x  = 0,
                              pos_y  = 64,
                              width  = function(w,h) return w/2-181 end,
                              height = function(w,h) return h-64-128 end
                          },
                          ["SysApp.Right"] = {
                              id     = 10,
                              pos_x  = function(w,h) return w/2+181 end,
                              pos_y  = 64,
                              width  = function(w,h) return w/2-181 end,
                              height = function(w,h) return h-64-128 end
                          },
                        }
               },
               { name  = "Mid",
                 zone  = "driver",
                 id    = 1
               }

  },
  layers = { {      0, "Background"   , 1 },
             {      1, "Application"  , 2 },
             {      2, "Softkeyboard" , 4 },
             {      3, "HomeScreen"   , 2 },
             {      4, "ControlBar"   , 2 },
             {      5, "InterruptApp" , 2 },
             {      6, "OnScreen"     , 2 },
             {    101, "Input"        , 3 },
             {    102, "Cursor"       , 5 },
             {    103, "Startup"      , 6 },
             { 0x1000, "Background"   , 1 },
             { 0x2000, "Normal"       , 2 },
             { 0x3000, "Fullscreen"   , 2 },
             { 0x4000, "InputPanel"   , 3 },
             { 0xA000, "Touch"        , 4 },
             { 0xB000, "Cursor"       , 5 },
             { 0xC000, "Startup"      , 6 }
  },


  manager_update = function(self, oper)
                       if verbose > 0 then
                           print("### <== WINDOW MANAGER UPDATE:" ..
                                 window_manager_operation_name(oper))
                       end
                       if oper == 1 then
                           local umask = window_mask { raise   = true,
                                                       visible = true,
                                                       active  = true }
                           local rmask = window_mask { active  = true }
                           local req = m:JSON({
                                       passthrough_update  = umask:tointeger(),
                                       passthrough_request = rmask:tointeger()
                           })
                           self:manager_request(req)
                       end
                   end,

  window_update = function(self, oper, win, mask)
                      if verbose > 0 then
                          print("### <== WINDOW UPDATE oper:" ..
                                window_operation_name(oper) ..
                                " mask: " .. tostring(mask))
                          if verbose > 1 then
                              print(win)
                          end
                      end

                      local arg = m:JSON({ surface = win.surface,
                                           winname = win.name,
                      })
                      local command = 0

                      if oper == 1 then -- create
                           local layertype = win.layertype
                           if layertype and input_layer[layertype] then
                               if verbose > 0 then
                                   print("ignoring input panel creation")
                               end
                               return
                           end
                           command     = 0x10001
                      elseif oper == 2 then -- destroy
                           command     = 0x10002
                      elseif oper == 3 then  -- namechange
                           command     = 0x10009
		      elseif oper == 4 or oper == 5 then --visible or configure
                           command     = 0x10008
                           arg.zone    = win.area
                           arg.node    = win.node
                           arg.layer   = win.layer
                           arg.pos_x   = win.pos_x
                           arg.pos_y   = win.pos_y
                           arg.width   = win.width
                           arg.height  = win.height
                           arg.raise   = win.raise
                           arg.visible = win.visible
                           arg.active  = win.active
                      elseif oper == 6 then -- active
                           command     = 0x10006
                           arg.active  = win.active
                      else
                           if verbose > 0 then
                               print("### nothing to do")
                           end
                           return
                      end

                      local msg = m:JSON({ command = command,
                                           appid   = win.appid,
                                           pid     = win.pid,
                                           arg     = arg
                      })
                      if verbose > 0 then
                          print("### <== sending " ..
                                command_name(msg.command) ..
                                " window message to '" .. win.name .. "'")
                          if verbose > 1 then
                              print(msg)
                          end
                      end
                      sc:send_message(sysctlid, msg)

                      if oper == 1 then -- create
                          local i = input_layer[win.layertype]
                          local p = self:application(win.appid)
                          local s = p.privileges.screen

                          if s == "system" then
                              local a = animation({})
                              local r = m:JSON({surface = win.surface,
                                                visible = 1,
                                                raise   = 1})
                              self:window_request(r,a,0)
                          else
                              if i then
                                  if verbose > 0 then
                                      print("do not make resource for " ..
                                            "input window")
                                  end
                              else
                                  resclnt:resource_set_create("screen",
                                                               "driver",
                                                               win.appid,
                                                               win.surface)
                              end
                          end
                      elseif oper == 2 then -- destroy
                          resclnt:resource_set_destroy("screen", win.surface)
                      elseif oper == 6 then -- active
                          if win.active then
                              local i = input_layer[win.layertype]
                              local p = self:application(win.appid)
                              local s = p.privileges.screen
                              local surface = win.surface
                              if not i and s ~= "system" then
                                 resclnt:resource_set_acquire("screen",surface)
                                 resmgr:window_raise(win.appid, surface, 1)
                              end
                          end
                      end
                  end,

  layer_update = function(self, oper, layer, mask)
                      if verbose > 0 then
                          print("### LAYER UPDATE:" .. 
                                layer_operation_name(oper) ..
                                " mask: " .. tostring(mask))
                          if verbose > 1 then
                              print(layer)
                          end
                      end
                      if oper == 3 then -- visible
                         local command = 0x10022
                         local msg = m:JSON({
                                         command = command,
                                         appid = "",
                                         arg = m:JSON({layer = layer.id,
                                                       visible = layer.visible
                                         })
                                })
                         if verbose > 0 then
                            print("### <== sending "..command_name(command)..
                                  " layer message")
                            if verbose > 1 then
                                print(msg)
                            end
                         end
                         sc:send_message(sysctlid, msg)
                      else
                           if verbose > 0 then
                               print("### nothing to do")
                           end
                      end
                 end,

  output_update = function(self, oper, out, mask)
                      local idx = out.index
                      if verbose > 0 then
                          print("### OUTPUT UPDATE:" .. oper ..
                                " mask: "..tostring(mask))
                      end
                      print(out)
                      local outdef = self.outputs[idx+1]
                      if (oper == 1) then -- create
                          if outdef then
                              self:output_request(m:JSON({index = idx,
                                                          id    = outdef.id,
                                                          name  = outdef.name
                                                          }))
                          end
                      elseif (oper == 5) then -- done
                          local ads = outdef.areas
                          local on = outdef.name
                          if ads then
                              for name,ad in pairs(ads) do
                                  local can = wmgr:canonical_name(on.."."..name)
                                  local a = m:JSON({name   = name,
                                                    output = out.index})
                                  for fld,val in pairs(ad) do
                                      a[fld] = self:geometry(out.width,
                                                             out.height,
                                                             val) 
                                   end
                                   self:area_create(a)
                                   resmgr:area_create(area[can], outdef.zone)
                              end
                          end
                      end
                  end
}


sc = m:get_system_controller()

-- resource sets
sets = {}

connected = false
sysctlid = ""

-- these shoud be before wmgr:connect() is called
if verbose > 0 then
   print("====== creating applications ======")
end
application {
    appid          = "default",
    area           = "Center.Full",
    privileges     = { screen = "none", audio = "none" },
    resource_class = "player",
    screen_priority = 0
}

application {
    appid           = "weston",
    area            = "Center.Full",
    privileges      = { screen = "system", audio = "none" },
    resource_class  = "implicit",
    screen_priority = 30
}

application {
    appid           = "org.tizen.ico.homescreen",
    area            = "Center.Full",
    privileges      = { screen = "system", audio = "system" },
    resource_class  = "player",
    screen_priority = 20
}

application {
    appid           = "org.tizen.ico.statusbar",
    area            = "Center.Status",
    privileges      = { screen = "system", audio = "none" },
    resource_class  = "player",
    screen_priority = 20 
}


if sc then
    sc.client_handler = function (self, cid, msg)
        local command = msg.command
        if verbose > 0 then
            print('### ==> client handler:')
            if verbose > 1 then
                print(msg)
            end
        end
        if not connected then
            print('Setting sysctlid='..msg.appid)
            sysctlid = msg.appid
            print('Trying to connect to wayland...')
            connected = wmgr:connect()
        end
        if connected and command then
            if command == 1 then -- send_appid
                local reply = m:JSON({ command = 0x60001,
                                       arg     = m:JSON({ stateid = 1,
                                                          state   = 0})
                                     })
                 if verbose > 0 then
                     print("### <== sending " ..
                           command_name(command) .. " message")
                     if verbose > 1 then
                         print(reply)
                     end
                 end
                 sc:send_message(sysctlid, reply)

                 reply = m:JSON({ command = 0x60001,
                                  arg     = m:JSON({ stateid = 2,
                                                     state   = 0})
                                })
                 if verbose > 0 then
                     print("### <== sending " ..
                           command_name(command) .. " message")
                     if verbose > 1 then
                         print(reply)
                     end
                 end
                 sc:send_message(sysctlid, reply)
            end
        end
    end

    sc.generic_handler = function (self, cid, msg)
        if verbose > 0 then
            print('### ==> generic handler:')
            if verbose > 1 then
               print(msg)
            end
        end
    end

    sc.window_handler = function (self, cid, msg)
        if verbose > 0 then
            print('### ==> received ' ..
                   command_name(msg.command) .. ' message')
            if verbose > 1 then
                print(tostring(msg))
            end
        end

        local a = animation({})
        local nores = false
        if msg.command == 0x10003 then       -- ico SHOW command
            local raise_mask = 0x01000000
            local lower_mask = 0x02000000
            local nores_mask = 0x40000000
            local time_mask  = 0x00ffffff
            msg.arg.visible = 1
            if msg.arg then
                local time = 200
                if  msg.arg.anim_time then
                    local t = msg.arg.anim_time
                    time = m:AND(t, time_mask)
                    nores = m:AND(t, nores_mask)
                    if m:AND(t, raise_mask) then
                        msg.arg.raise = 1
                    elseif m:AND(t, lower_mask) then
                        msg.arg.raise = 0
                    end
                end
                if msg.arg.anim_name then
                    a.show = { msg.arg.anim_name, time }
                    print('time: ' .. tostring(a.show[2]))
                end
            end
            if not nores then
                local p = wmgr:application(msg.appid)
                local s = p.privileges.screen
                if s == "system" then
                    nores = true
                end
            end
            if verbose > 2 then
                print('### ==> SHOW')
                print(tostring(msg.arg))
            end
            if nores then
                wmgr:window_request(msg.arg, a, 0)
            else
                local surface = msg.arg.surface
                resclnt:resource_set_acquire("screen", surface)
                resmgr:window_raise(msg.appid, surface, 1)
            end
        elseif msg.command == 0x10004 then   -- ico HIDE command
            local raise_mask = 0x01000000
            local lower_mask = 0x02000000
            local nores_mask = 0x40000000
            local time_mask  = 0x00ffffff
            msg.arg.visible = 0
            if msg.arg then
                local time = 200
                if msg.arg.anim_time then
                    local t = msg.arg.anim_time
                    time = m:AND(t, time_mask)
                    nores = m:AND(t, nores_mask)
                end
                if msg.arg.anim_name then
                    a.hide = { msg.arg.anim_name, time }
                    print('hide animation time: ' .. tostring(a.hide[2]))
                end
            end
            if not nores then
                local p = wmgr:application(msg.appid)
                local s = p.privileges.screen
                if s == "system" then
                    nores = true
                end
            end
            if verbose > 2 then
                print('### ==> HIDE REQUEST')
                print(tostring(msg.arg))
            end
            if nores then
                wmgr:window_request(msg.arg, a, 0)
            else
                resmgr:window_raise(msg.appid, msg.arg.surface, -1)
            end
        elseif msg.command == 0x10005 then   -- ico MOVE
            if verbose > 2 then
                print('### ==> MOVE REQUEST')
                print(tostring(msg.arg))
            end
            wmgr:window_request(msg.arg, a, 0)
            -- TODO: handle if area changed
        elseif msg.command == 0x10006 then   -- ico ACTIVE
            if not msg.arg.active then
                msg.arg.active = 3 -- pointer + keyboard
            end
            if verbose > 2 then
                print('### ==> ACTIVE REQUEST')
                print(tostring(msg.arg))
            end
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10007 then   -- ico CHANGE_LAYER
            if verbose > 2 then
                print('### ==> CHANGE_LAYER REQUEST')
                print(tostring(msg.arg))
            end
            --[[
            if msg.arg.layer ~= 4 or msg.arg.layer ~= 5 then
                print("do not change layer for other than cursor or touch")
                return
            end
            --]]
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10011 then   -- ico MAP_THUMB
            local framerate = msg.arg.framerate
            if not framerate or framerate < 0 then
                framerate = 0
            end
            msg.arg.map = 1
            if verbose > 2 then
                print('### ==> MAP_THUMB REQUEST')
                print(msg.arg)
                print('framerate: '..framerate)
            end
            wmgr:window_request(msg.arg, a, framerate)
        elseif msg.command == 0x10012 then   -- ico UNMAP_THUMB
            msg.arg.map = 0
            if verbose > 2 then
                print('### ==> UNMAP_THUMB REQUEST')
                print(msg.arg)
            end
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10020 then   -- ico SHOW_LAYER command
            msg.arg.visible = 1
            if verbose > 2 then
                print('### ==> SHOW_LAYER REQUEST')
                print(msg.arg)
            end
            wmgr:layer_request(msg.arg)
        elseif msg.command == 0x10021 then   -- ico HIDE_LAYER command
            msg.arg.visible = 0
            if verbose > 2 then
                print('### ==> HIDE_LAYER REQUEST')
                print(msg.arg)
            end
            wmgr:layer_request(msg.arg)
        end
    end

    sc.input_handler = function (self, cid, msg)
        if verbose > 0 then
            print('### ==> input handler: ' .. command_name(msg.comand))
            if verbose > 1 then
                print(msg)
            end
        end
    end

    sc.user_handler = function (self, cid, msg)
        if verbose > 0 then
            print('### ==> user handler: ' .. command_name(msg.command))
            if verbose > 1 then
                print(msg)
            end
        end
    end

    sc.resource_handler = function (self, cid, msg)
        if verbose > 0 then
            print('### ==> resource handler: ' .. command_name(msg.command))
            if verbose > 1 then
                print(msg)
            end
        end

        createResourceSet = function (ctl, client, msg)
            cb = function(rset, data)
                print("> resource callback")

                -- type is either basic (0) or interrupt (1)
                requestType = 0
                if msg.res.type then
                    requestType = msg.res.type
                end

                if rset.acquired then
                    cmd = 0x00040001 -- acquire
                else
                    cmd = 0x00040002 -- release
                end

                reply = m.JSON({
                        appid = data.client,
                        command = cmd,
                        res = {
                            type = requestType
                        }
                    })

                if rset.resources.audio_playback then
                    reply.res.sound = {
                        zone = "driver",
                        name = msg.appid,
                        adjust = 0,
                        -- id = "0"
                    }
                end

                if rset.resources.display then
                    reply.res.window = {
                        zone = "driver",
                        name = msg.appid,
                        -- id = "0"
                    }
                end

                if rset.resources.input then
                    reply.res.input = {
                        name = msg.appid,
                        event = 0
                    }
                end
                print("sending message to client: " .. data.client)

                if sc:send_message(data.client, reply) then
                    print('*** reply OK')
                else
                    print('*** reply FAILED')
                end
            end

            rset = m:ResourceSet({
                    application_class = "player",
                    zone = "driver", -- msg.zone ("full")
                    callback = cb
                })

            rset.data = {
                cid = cid,
                ctl = ctl
            }

            if msg.res.sound then
                rset:addResource({
                        resource_name = "audio_playback"
                    })
                rset.resources.audio_playback.attributes.pid = tostring(msg.pid)
                rset.resources.audio_playback.attributes.appid = msg.appid
                print("sound name: " .. msg.res.sound.name)
                print("sound zone:" .. msg.res.sound.zone)
                print("sound adjust: " .. tostring(msg.res.sound.adjust))
                if msg.res.sound.id then
                    print("sound id: " .. msg.res.sound.id)
                end
            end

            if msg.res.input then
                rset:addResource({
                        resource_name = "input"
                    })
                rset.resources.input.attributes.pid = tostring(msg.pid)
                rset.resources.input.attributes.appid = msg.appid
                print("input name: " .. msg.res.sound.name)
                print("input event:" .. tostring(msg.res.input.event))
            end

            if msg.res.window then
                rset:addResource({
                        resource_name = "display"
                    })
                rset.resources.display.attributes.pid = tostring(msg.pid)
                rset.resources.display.attributes.appid = msg.appid
                print("display name: " .. msg.res.display.name)
                print("display zone:" .. msg.res.display.zone)
                if msg.res.display.id then
                    print("display id: " .. msg.res.display.id)
                end
            end

            return rset
        end

        -- parse the message

        -- fields common to all messages:
        --      msg.command
        --      msg.appid
        --      msg.pid

        if msg.command == 0x00040011 then -- MSG_CMD_CREATE_RES
            print("command CREATE")

            if not sets.cid then
                sets.cid = createResourceSet(self, cid, msg)
            end

        elseif msg.command == 0x00040012 then -- MSG_CMD_DESTORY_RES
            print("command DESTROY")

            if sets.cid then
                sets.cid:release()
            end

            sets.cid = nil -- garbage collecting

        elseif msg.command == 0x00040001 then -- MSG_CMD_ACQUIRE_RES
            print("command ACQUIRE")

            if not sets.cid then
                sets.cid = createResourceSet(self, cid, msg)
            end

            sets.cid:acquire()

        elseif msg.command == 0x00040002 then -- MSG_CMD_RELEASE_RES
            print("command RELEASE")

            if sets.cid then
                sets.cid:release()
            end

        elseif msg.command == 0x00040003 then -- MSG_CMD_DEPRIVE_RES
            print("command DEPRIVE")

        elseif msg.command == 0x00040004 then -- MSG_CMD_WAITING_RES
            print("command WAITING")

        elseif msg.command == 0x00040005 then -- MSG_CMD_REVERT_RES
            print("command REVERT")
        end
    end

    sc.inputdev_handler = function (self, cid, msg)
        if verbose > 0 then
            print('*** inputdev handler: ' .. command_name(msg.command))
            if verbose > 1 then
                print(msg)
            end
        end
    end
end
