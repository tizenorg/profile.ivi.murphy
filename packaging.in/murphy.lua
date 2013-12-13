with_system_controller = false

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
    m:load_plugin('amb')
else
    m:info("No amb plugin found...")
end

-- load the ASM resource plugin
if m:plugin_exists('resource-asm') then
    m:load_plugin('resource-asm', {
        zone = "driver",
        share_mmplayer = "player:AVP,mandatory,exclusive,strict",
        ignored_argv0 = "WebProcess"
    })
else
    m:info("No audio session manager plugin found...")
end

if m:plugin_exists('ivi-resource-manager') and not with_system_controller then
    m:load_plugin('ivi-resource-manager')
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
if not m:plugin_exists('ivi-resource-manager') and
   not with_system_controller
then
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

sink.lua {
    name = "night_mode",
    inputs = { owner = mdb.select.select_night_mode },
    property = "NightMode",
    type = "b",
    initiate = builtin.method.amb_initiate,
    update = builtin.method.amb_update
}


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

sink.lua {
    name = "driving_mode",
    inputs = { owner = mdb.select.select_driving_mode },
    property = "DrivingMode",
    type = "u",
    initiate = builtin.method.amb_initiate,
    update = builtin.method.amb_update
}

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

if not with_system_controller then
   return
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
    [0x20003] = "send_input"
}

function command_name(command)
    local name = command_names[command]
    if name then return name end
    return "<unknown " .. tostring(command) .. ">"
end

wmgr = window_manager({
  geometry = function(self, w,h, v)
                  if type(v) == "function" then
                      return v(w,h)
                  end
                  return v
             end,

  outputs = { { name  = "Center",
                id    = 0,
                areas = { Status = {
                              id     = 100,
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
             {    102, "Touch"        , 4 },
             {    103, "Cursor"       , 5 },
             { 0x1000, "Background"   , 1 },
             { 0x2000, "Normal"       , 2 },
             { 0x3000, "Fullscreen"   , 2 },
             { 0x4000, "InputPanel"   , 3 },
             { 0xA000, "Touch"        , 4 },
             { 0xB000, "Cursor"       , 5 },
             { 0xC000, "Startup"      , 6 }
  },


  window_update = function(self, oper, win, mask)
                      if (verbose) then
                          print("*** WINDOW UPDATE oper:"..window_operation_name(oper).." mask: "..tostring(mask))
                          print(win)
                      end

                      local arg = m:JSON({ surface = win.surface,
                                           winname = win.name,
                      })
                      local command = 0

                      if oper == 1 then -- create
                           if win.layertype and win.layertype == 3 then
                               print("ignoring input panel window creation")
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
                           if false and win.active ~= 0 then
                               print("### already active")
                               return
                           end
                           command = 0x10006
                      else
                           print("### nothing to do")
                           return
                      end

                      local msg = m:JSON({ command = command,
                                           appid   = win.appid,
                                           pid     = win.pid,
                                           arg     = arg
                      })
                      print("### sending window message: " .. command_name(msg.command))
                      print(msg)
                      sc:send_message(msg.appid, msg)

                      if (oper == 1) then -- create
                          local a = animation({})
                          local r = m:JSON({surface = win.surface,
                                            visible = 1,
                                            raise   = 1})
                          self:window_request(r,a,0)
                      end
                  end,

  layer_update = function(self, oper, j, mask)
                      if verbose then
                          print("*** LAYER UPDATE oper:"..oper.." mask: "..tostring(mask))
                          print(j)
                      end
                 end,
  output_update = function(self, oper, out, mask)
                      local idx = out.index
                      if verbose then
                          print("*** OUTPUT UPDATE oper:"..oper.." mask: "..tostring(mask))
                      end
                      print(out)
                      if (oper == 1) then -- create
                          local outdef = self.outputs[idx+1]
                          if outdef then
                              self:output_request(m:JSON({index = idx,
                                                          id    = outdef.id,
                                                          name  = outdef.name
                                                          }))
                          end
                      elseif (oper == 5) then -- done
                          local ads = self.outputs[idx+1].areas
                          if ads then
                              for name,area in pairs(ads) do
                                  local a = m:JSON({name=name,output=out.index})
                                  for fld,val in pairs(area) do
                                      a[fld] = self:geometry(out.width,out.height,val)
                                   end
                                   self:area_create(a)
                              end
                          end
                      end
                  end
})


sc = m:get_system_controller()

connected = false

-- these shoud be before wmgr:connect() is called
print("====== creating applications")
application {
    appid      = "default",
    area       = "Center.Full",
    privileges = { screen = "none", audio = "none" }
}

application {
    appid      = "org.tizen.ico.homescreen",
    area       = "Center.Full",
    privileges = { screen = "system", audio = "system" }
}

application {
    appid      = "org.tizen.ico.statusbar",
    area       = "Center.Status",
    privileges = { screen = "system", audio = "none" }
}


if sc then
    sc.client_handler = function (self, cid, msg)
        print('*** client handler: ' .. tostring(msg))
        if not connected then
            print('Trying to connect to wayland...')
            connected = wmgr:connect()
        else
            print('Window manager already connected...')
        end
    end

    sc.generic_handler = function (self, cid, msg)
        print('*** generic handler: ' .. tostring(msg))
    end

    sc.window_handler = function (self, cid, msg)
        print('*** window handler: ' .. command_name(msg.command) .. ' ' .. tostring(msg))

        local a = animation({})
        if msg.command == 0x10003 then       -- ico SHOW command
            local raise_mask = 0x01000000
            local lower_mask = 0x02000000
            msg.arg.visible = 1
            if msg.arg and msg.arg.anim_name then
                local time = msg.arg.time
                time = m:AND(time, m:NEG(m:OR(raise_mask, lower_mask)))
                time = 200
                if m:AND(msg.arg.anim_time, raise_mask) then
                    msg.arg.raise = 1
                elseif m:AND(msg.arg.anim_time, lower_mask) then
                    msg.arg.raise = 0
                end
                a.show = { msg.arg.anim_name, time }
                print('time: ' .. tostring(a.show[2]))
            end
            print('##### SHOW')
            print(tostring(msg.arg))
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10004 then   -- ico HIDE command
            local raise_mask = 0x01000000
            local lower_mask = 0x02000000
            msg.arg.visible = 0
            if msg.arg and msg.arg.anim_name then
                local time = msg.arg.time
                time = m:AND(time, m:NEG(m:OR(raise_mask, lower_mask)))
                time = 200
                if m:AND(msg.arg.anim_time, raise_mask) then
                    msg.arg.raise = 1
                end
                if m:AND(msg.arg.anim_time, lower_mask) then
                    msg.arg.raise = 0
                end
                a.hide = { msg.arg.anim_name, time }
                print('hide animation time: ' .. tostring(a.hide[2]))
            end
            print('##### HIDE')
            print(tostring(msg.arg))
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10005 then   -- ico MOVE
            print('##### MOVE')
            print(tostring(msg.arg))
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10006 then   -- ico ACTIVE
            print('##### ACTIVE')
            if not msg.arg.active then
                msg.arg.active = 0
            end
            print(tostring(msg.arg))
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10007 then   -- ico CHANGE_LAYER
            print('##### CHANGE_LAYER')
            print(tostring(msg.arg))
            --[[
            if msg.arg.layer ~= 4 or msg.arg.layer ~= 5 then
                print("do not change layer for other than cursor or touch")
                return
            end
            --]]
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10011 then   -- ico MAP_THUMB
            local framerate = msg.arg.framerate
            print('##### MAP_THUMB')
            msg.arg.map = 1
            if not framerate or framerate < 0 then
                framerate = 0
            end
            wmgr:window_request(msg.arg, a, framerate)
        elseif msg.command == 0x10012 then   -- ico UNMAP_THUMB
            print('##### UNMAP_THUMB')
            msg.arg.map = 0
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10020 then   -- ico SHOW_LAYER command
            msg.arg.visible = 1
            print('##### SHOW_LAYER')
            wmgr:layer_request(msg.arg)
        end
    end
    sc.input_handler = function (self, cid, msg)
        print('*** input handler: ' .. tostring(msg))
    end
    sc.user_handler = function (self, cid, msg)
        print('*** user handler: ' .. tostring(msg))
    end
    sc.resource_handler = function (self, cid, msg)
        print('*** resource handler: ' .. tostring(msg))

        reply = m.JSON({ command = 'reply to client #' .. tostring(cid) })

        if sc:send_message(self, cid, reply) then
            print('*** reply OK')
        else
            print('*** reply FAILED')
        end
    end
    sc.inputdev_handler = function (self, cid, msg)
        print('*** inputdev handler: ' .. tostring(msg))
    end
end
