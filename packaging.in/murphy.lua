with_system_controller = false
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

if m:plugin_exists('glib') then
    m:load_plugin('glib')
else
    m:info("No glib plugin found...")
end

if m:plugin_exists("gam-resource-manager") then

function get_general_priorities(self)
    print("*** get_general_priorities\n")
    return { "USB Headset", "wiredHeadset", "speakers" }
end

function get_phone_priorities(self)
    print("*** get_phone_priorities\n")
    return { "wiredHeadset", "USB Headset" }
end

m:load_plugin('gam-resource-manager', {
    config_dir = '/etc/murphy/gam',
    decision_names = 'gam-wrtApplication-4',
    max_active = 4,
    app_mapping = {
        ['t8j6HTRpuz.MediaPlayer'] = 'wrtApplication',
        ['pacat'] = 'icoApplication'
    },
    app_default = 'icoApplication'
})

routing_sink_priority {
    application_class = "player",
    priority_queue = get_general_priorities
}

routing_sink_priority {
    application_class = "game",
    priority_queue = get_general_priorities
}

routing_sink_priority {
    application_class = "implicit",
    priority_queue = get_general_priorities
}

routing_sink_priority {
    application_class = "phone",
    priority_queue = get_phone_priorities
}

routing_sink_priority {
    application_class = "basic",
    priority_queue = get_phone_priorities
}

routing_sink_priority {
    application_class = "event",
    priority_queue = get_phone_priorities
}
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

if m:plugin_exists('system-controller') then
    with_system_controller = true
elseif m:plugin_exists('ivi-resource-manager') then
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
    name     = "system",
    priority = 52,
    modal    = false,
    share    = true,
    order    = "lifo"
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
    share    = false,
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
   not with_system_controller and
   not m:plugin_exists('gam-resource-manager')
then
   resource.class {
        name = "audio_playback",
        shareable = true,
        attributes = {
            role    = { mdb.string, "music", "rw" },
            pid     = { mdb.string, "<unknown>", "rw" },
            policy  = { mdb.string, "relaxed", "rw" },
            source  = { mdb.string, "webkit", "rw" },
            conn_id = { mdb.unsigned, 0, "rw" },
            name    = { mdb.string, "<unknown>", "rw" },
        }
   }
end

if not m:plugin_exists('gam-resource-manager') then
    resource.class {
        name = "audio_recording",
        shareable = true,
        attributes = {
             role   = { mdb.string, "music"    , "rw" },
             pid    = { mdb.string, "<unknown>", "rw" },
             policy = { mdb.string, "relaxed"  , "rw" },
             name   = { mdb.string, "<unknown>", "rw" },
         }
    }
end

resource.class {
     name = "video_playback",
     shareable = false,
}

resource.class {
     name = "video_recording",
     shareable = false
}

resource.class {
     name = "speech_recognition",
     shareable = true
}

resource.class {
     name = "speech_synthesis",
     shareable = true
}

-- PulseAudio volume context
mdb.table {
    name = "volume_context",
    index = { "id" },
    create = true,
    columns = {
        { "id", mdb.unsigned },
        { "value", mdb.string, 64 },
    }
}

-- put default volume context to the table
mdb.table.volume_context:insert({ id = 1, value = "default" })

if not m:plugin_exists('ivi-resource-manager') and
   not with_system_controller
then
    resource.method.veto = {
        function(zone, rset, grant, owners, req_set)
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

mdb.select {
    name = "amb_state",
    table = "amb_state",
    columns = { "state" },
    condition = "id = 0"
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
        inputs = { NightMode = mdb.select.select_night_mode,
                   amb_state = mdb.select.amb_state },
        property = "NightMode",
        type = "b",
        initiate = builtin.method.amb_initiate,
        update = builtin.method.amb_update
    }
end

-- Night mode general handlers

if with_system_controller then
    sink.lua {
        name = "nightmode_homescreen",
        inputs = { owner = mdb.select.select_night_mode },
        initiate = function(self)
                -- data = mdb.select.select_night_mode.single_value
                return true
            end,
        update = function(self)
                send_night_mode_to(homescreen)
            end
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
        inputs = { DrivingMode = mdb.select.select_driving_mode,
                   amb_state = mdb.select.amb_state },
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

-- define three categories

mdb.select {
    name = "undefined_applications",
    table = "aul_applications",
    columns = { "appid" },
    condition = "category = '<undefined>'"
}

mdb.select {
    name = "basic_applications",
    table = "aul_applications",
    columns = { "appid" },
    condition = "category = 'basic'"
}

mdb.select {
    name = "entertainment_applications",
    table = "aul_applications",
    columns = { "appid" },
    condition = "category = 'entertainment'"
}

function ft(t)
    -- filter the object garbage out of the tables
    ret = {}

    for k,v in pairs(t) do
        if k ~= "userdata" and k ~= "new" then
            ret[k] = v
        end
    end

    return ret
end

function getApplication(appid)
    local conf = nil

    -- find the correct local application definition

    for k,v in pairs(ft(application)) do
        if appid == v.appid then
            conf = v
            break
        end
    end

    return conf
end

function regulateApplications(t, regulation)
    for k,v in pairs(ft(t)) do

        whitelisted = false

        -- our local application config, which takes precedence
        local conf = getApplication(v.appid)

        if conf then
            if conf.resource_class ~= "player" then
                whitelisted = true
            end

	       if conf.requisites and conf.requisites.screen then
                if conf.requisites.screen.driving then
                    whitelisted = true
                end
            end
        end

        if whitelisted then
            -- override, don't disable
            resmgr:disable_screen_by_appid("*", "*", v.appid, false, false)
        else
            resmgr:disable_screen_by_appid("*", "*", v.appid, regulation == 1, false)
        end
    end
    resource.method.recalc("driver")
end

-- regulation (on), use "select_driving_mode"

sink.lua {
    name = "driving_regulation",
    inputs = { owner = mdb.select.select_driving_mode },
    initiate = function(self)
        -- local data = mdb.select.select_driving_mode.single_value
        return true
    end,
    update = function(self)
        local data = mdb.select.select_driving_mode.single_value

        if verbose > 1 then
            print("Driving mode updated: " .. tostring(data))
        end

        if not sc then
            return true
        end

        -- tell homescreen that driving mode was updated
        send_driving_mode_to(homescreen)

        regulateApplications(ft(mdb.select.entertainment_applications), data)
        regulateApplications(ft(mdb.select.undefined_applications), data)

        return true
    end
}
--[[
sink.lua {
    name = "regulated_app_change",
    inputs = { undef = mdb.select.undefined_applications,
               entertainment = mdb.select.entertainment_applications },
    initiate = function(self)
        return true
    end,
    update = function(self)
        local data = mdb.select.select_driving_mode.single_value

        if not sc then
            return
        end

        if verbose > 1 then
            print("regulated application list was changed")
        end

        regulateApplications(ft(mdb.select.entertainment_applications), data)
        regulateApplications(ft(mdb.select.undefined_applications), data)

        return true
    end
}
--]]

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

-- system controller test setup

if not with_system_controller then
   -- ok, we should have 'audio_playback' defined by now
   m:try_load_plugin('telephony')
   return
end

m:load_plugin('system-controller')

onscreen_counter = 0

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
    [6] = "active",
    [7] = "map",
    [8] = "hint"
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

input_manager_operation_names = {
    [1] = "create",
    [2] = "destroy",
    [3] = "ready"
}

function input_manager_operation_name(oper)
    local name = input_manager_operation_names[oper]
    if name then return name end
    return "<unknown " .. tostring(oper) .. ">"
end

input_operation_names = {
    [1] = "create",
    [2] = "destroy",
    [3] = "update"
}

function input_operation_name(oper)
    local name = input_operation_names[oper]
    if name then return name end
    return "<unknown " .. tostring(oper) .. ">"
end

code_operation_names = {
    [1] = "create",
    [2] = "destroy",
    [3] = "state_change"
}

function code_operation_name(oper)
    local name = code_operation_names[oper]
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
    [0x10006] = "animation",
    [0x10007] = "change_active",
    [0x10008] = "change_layer",
    [0x10009] = "change_attr",
    [0x10010] = "name",
    [0x10020] = "map_thumb",
    [0x10021] = "unmap_thumb",
    [0x10022] = "map_get",
    [0x10030] = "show layer",
    [0x10031] = "hide_layer",
    [0x10032] = "change_layer_attr",
    [0x20001] = "add_input",
    [0x20002] = "del_input",
    [0x30001] = "change_user",
    [0x30002] = "get_userlist",
    [0x30003] = "get_lastinfo",
    [0x30004] = "set_lastinfo",
    [0x40001] = "acquire_res",
    [0x40002] = "release_res",
    [0x40003] = "deprive_res",
    [0x40004] = "waiting_res",
    [0x40005] = "revert_res",
    [0x40006] = "window_id_res",
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
   [101] = true, -- input
   [102] = true, -- touch
   [103] = true  -- cursor
}

-- some day this should be merged with wmgr.layers
ico_layer_type = {
   [1]   = 0x1000, -- background
   [2]   = 0x2000, -- application
   [3]   = 0x2000, -- homescreen
   [4]   = 0x2000, -- interrupt application
   [5]   = 0x2000, -- onscreen application
   [6]   = 0xc000, -- startup
   [7]   = 0x3000, -- fullscreen
   [101] = 0x4000, -- input
   [102] = 0xa000, -- touch
   [103] = 0xb000  -- cursor
}

resmgr = resource_manager {
  screen_event_handler = function(self, ev)
                             local event = ev.event
                             local surface = ev.surface

                             if event == "init" then
                                 if verbose > 0 then
                                     print("*** init screen resource allocation -- disable all 'player'")
                                 end
                                 resmgr:disable_audio_by_appid("*", "player", "*", true, false)
                             elseif event == "preallocate" then
                                 if verbose > 0 then
                                     print("*** preallocate screen resource "..
                                           "for '" .. ev.appid .. "' -- enable 'player', if any")
                                 end
                                 resmgr:disable_audio_by_appid("*", "player", ev.appid, false, false)
                             elseif event == "grant" then
                                 if verbose > 0 then
                                    print("*** make visible surface "..surface)
                                 end
                                 local a = animation({})
                                 local r = m:JSON({surface = surface,
                                                   visible = 1,
                                                   raise   = 1})
                                 if ev.appid == onscreen then
                                     onscreen_counter = onscreen_counter + 1
                                     wmgr:layer_request(m:JSON({layer = 5, visible = 1}))
                                 end

                                 wmgr:window_request(r,a,0)
                             elseif event == "revoke" then
                                 if verbose > 0 then
                                    print("*** hide surface "..surface)
                                 end
                                 local a = animation({})
                                 local r = m:JSON({surface = ev.surface,
                                                   visible = 0})
                                 if ev.appid == onscreen then
                                     onscreen_counter = onscreen_counter - 1
                                     if onscreen_counter <= 0 then
                                        onscreen_counter = 0
                                        wmgr:layer_request(m:JSON({layer = 5, visible = 0}))
                                     end
                                 end
                                 wmgr:window_request(r,a,0)

                             elseif event == "create" then

                                if verbose > 0 then
                                    print("*** screen resource event: " ..
                                          tostring(ev))
                                end

                                local regulation = mdb.select.select_driving_mode.single_value

                                if regulation == 1 then

                                    local blacklisted = false

                                    -- applications which have their category set to "entertainment"
                                    -- or "undefined" are blacklisted, meaning they should be regulated

                                    for i,v in pairs(ft(mdb.select.undefined_applications)) do
                                        if v.appid == ev.appid then
                                            if verbose > 0 then
                                                print(ev.appid .. " was blacklisted (undefined)")
                                            end
                                            blacklisted = true
                                            break
                                        end
                                    end

                                    if not blacklisted then
                                        for i,v in pairs(ft(mdb.select.entertainment_applications)) do
                                            if v.appid == ev.appid then
                                                if verbose > 0 then
                                                    print(ev.appid .. " was blacklisted (entertainment)")
                                                end
                                                blacklisted = true
                                                break
                                            end
                                        end
                                    end

                                    -- our local application config, which takes precedence
                                    local conf = getApplication(ev.appid)

                                    if not conf then
                                        blacklisted = true
                                    else
                                        if conf.resource_class == "player" then
                                            blacklisted = true
                                        end

                                        -- check the exceptions
                                        if conf.requisites and conf.requisites.screen then
                                            if conf.requisites.screen.driving then
                                                blacklisted = false
                                            end
                                        end
                                    end

                                    -- disable only non-whitelisted applications
				                    if blacklisted then
                                        if verbose > 0 then
                                            print("disabling screen for " .. ev.appid)
                                        end
                                        resmgr:disable_screen_by_appid("*", "*", ev.appid, true, true)
                                    end
                                end

                             elseif event == "destroy" then
                               if verbose > 0 then
                                    print("*** screen resource event: " ..
                                          tostring(ev))
                                 end
                             else
                                 if verbose > 0 then
                                    print("*** screen resource event: " ..
                                          tostring(ev))
                                 end
                             end
                         end,
  audio_event_handler = function(self, ev)
                             local event = ev.event
                             local appid = ev.appid
                             local audioid = ev.audioid

                             if event == "grant" then
                                 if verbose > 0 then
                                    print("*** grant audio to "..appid..
                                          " ("..audioid..") in '" ..
                                          ev.zone .. "' zone")
                                 end
                             elseif event == "revoke" then
                                 if verbose > 0 then
                                    print("*** revoke audio from "..appid..
                                          " ("..audioid..") in '" ..
                                          ev.zone .. "' zone")
                                 end
                             else
                                 if verbose > 0 then
                                    print("*** audio resource event: " ..
                                          tostring(ev))
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

  output_order = { 1, 0 },

  outputs = { { name  = "Mid",
                id    = 1,
                zone  = "driver",
                areas = { Full = {
                              id     = 20,
                              pos_x  = 0,
                              pos_y  = 0,
                              width  = function(w,h) return w end,
                              height = function(w,h) return h end
                          },
                          Left = {
                              id     = 21,
                              pos_x  = 0,
                              pos_y  = 0,
                              width  = 320,
                              height = function(w,h) return h end
                          },
                          Right = {
                              id     = 22,
                              pos_x  = function(w,h) return w-320 end,
                              pos_y  = 0,
                              width  = 320,
                              height = function(w,h) return h end
                          }
                        }
               },
               { name  = "Center",
                 id    = 4,
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
                           MobileFull = {
                              id     = 11,
                              pos_x  = 0,
                              pos_y  = 64,
                              width  = function(w,h) return w end,
                              height = function(w,h) return h-64-128 end
                           },
                           MobileUpper = {
                              id     = 12,
                              pos_x  = 0,
                              pos_y  = 64,
                              width  = function(w,h) return w end,
                              height = function(w,h) return (h-64-128)/2 end
                           },
                           MobileLower = {
                              id     = 13,
                              pos_x  = 0,
                              pos_y  = function(w,h) return (h-64-128)/2+64 end,
                              width  = function(w,h) return w end,
                              height = function(w,h) return (h-64-128)/2 end
                           },
                           Control = {
                              id     = 14,
                              pos_x  = 0,
                              pos_y  = function(w,h) return h-128 end,
                              width  = function(w,h) return w end,
                              height = 128
                           },
                        }
              }
  },
             --    id   name            type  output
  layers = { {      0, "BackGround"   ,    1, "Center" },
             {      1, "Application"  ,    2, "Center" },
             {      2, "HomeScreen"   ,    3, "Center" },
             {      3, "ControlBar"   ,    3, "Center" },
             {      4, "InterruptApp" ,    4, "Center" },
             {      5, "OnScreen"     ,    5, "Center" },
             {      6, "Touch"        ,  102, "Center" },
             {      7, "Cursor"       ,  103, "Center" }
  },


  manager_update = function(self, oper)
                       if verbose > 0 then
                           print("### <== WINDOW MANAGER UPDATE:" ..
                                 window_manager_operation_name(oper))
                       end
                       if oper == 1 then
                           local wumask = window_mask { --raise   = true,
                                                        visible = true,
                                                        active  = true }
                           local wrmask = window_mask { raise   = true,
                                                        active  = true,
                                                        layer   = true }
                           local lumask = layer_mask  { visible = true }
                           local lrmask = layer_mask  { visible = true }
                           local req = m:JSON({
                               passthrough_window_update  = wumask:tointeger(),
                               passthrough_window_request = wrmask:tointeger(),
                               passthrough_layer_update   = lumask:tointeger(),
                               passthrough_layer_request  = lrmask:tointeger()
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
                                           winname = win.name
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
                           command     = 0x10010
                      elseif oper == 4 or oper == 5 then -- visible/configure
                           command     = 0x10009
                           arg.zone    = win.area
                           arg.node    = win.node
                           if win.layertype then
                               arg.layertype = win.layertype
                           end
                           arg.layer   = win.layer
                           arg.pos_x   = win.pos_x
                           arg.pos_y   = win.pos_y
                           arg.width   = win.width
                           arg.height  = win.height
                           arg.raise   = win.raise
                           arg.visible = win.visible
                           if win.active == 0 then
                               arg.active = 0
                           else
                               arg.active = 1
                           end
                      elseif oper == 6 then -- active
                           if win.active == 0 then
                               if verbose > 0 then
                                   print("ignoring inactive event")
                               end
                               return
                           end
                           command = 0x10007
                      elseif oper == 7 then -- map
                           local map = win.map
                           if not map then
                               return
                           end
                           if win.mapped == 0 then
                               command = 0x10021
                           else
                               command = 0x10020
                           end
                           arg.attr = map.type
                           --arg.name = map.target
                           arg.width = map.width
                           arg.height = map.height
                           arg.stride = map.stride
                           arg.format = map.format
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
                                " window message to '" .. homescreen .. "'")
                          if verbose > 1 then
                              print(msg)
                          end
                      end
                      sc:send_message(homescreen, msg)

                      if oper == 1 then -- create
                          local i = input_layer[win.layertype]
                          local p = self:application(win.appid)
                          local s = p.privileges.screen

                          if s == "system" then
                              local a = animation({})
                              local r = m:JSON({surface = win.surface,
                                                visible = 0,
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
                                  special_screen_sets[win.surface] = true
                              end
                          end

                          if onscreen and win.appid == onscreen then
                              local resmsg = m:JSON({
                                        command = 0x40006, -- window_id_res
                                        appid   = win.appid,
                                        pid     = win.pid,
                                        res     = m:JSON({
                                            window  = m:JSON({
                                                ECU     = "",
                                                display = "",
                                                layer   = "",
                                                layout  = "",
                                                area    = "",
                                                dispatchApp = "",
                                                role        = win.name,
                                                resourceId  = win.surface
                                            })
                                        })
                              })
                              if verbose > 0 then
                                  print("### <== sending " ..
                                        command_name(resmsg.command) ..
                                        " message to '" .. onscreen .. "'")
                                  if verbose > 1 then
                                      print(resmsg)
                                  end
                              end
                              sc:send_message(onscreen, resmsg);
                          end
                      elseif oper == 2 then -- destroy
                          resclnt:resource_set_destroy("screen", win.surface)
                          special_screen_sets[win.surface] = nil
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
                         local command = 0x10008 -- change_layer
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
                         sc:send_message(homescreen, msg)
                      else
                           if verbose > 0 then
                               print("### nothing to do")
                           end
                      end
                 end,

  output_update = function(self, oper, out, mask)
                      local idx = out.index
                      local defidx = self.output_order[idx+1]
                      if verbose > 0 then
                          print("### OUTPUT UPDATE:" .. oper ..
                                " mask: "..tostring(mask))
                      end
                      if not defidx then
                          return
                      end
                      print(out)
                      local outdef = self.outputs[defidx+1]
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


imgr = input_manager {
  inputs = {{ name = "G27 Racing Wheel",
              id = 0,
              switch = { [2] = {appid="org.tizen.ico.app-soundsample"      },
                         [3] = {appid="org.tizen.ico.homescreen", keycode=1},
                         [4] = {appid="org.tizen.ico.app-soundsample"      },
                         [5] = {appid="org.tizen.ico.homescreen", keycode=2}
             }}
  },

  manager_update = function(self, oper)
                       if verbose > 0 then
                           print("### <== INPUT MANAGER UPDATE:" ..
                                 input_manager_operation_name(oper))
                       end
                   end,

  input_update = function(self, oper, inp, mask)
                     if verbose > 0 then
                         print("### INPUT UPDATE:" ..
                                input_operation_name(oper) ..
                                " mask: " .. tostring(mask))
                          if verbose > 1 then
                              print(inp)
                          end
                      end
                 end,
  code_update  = function(self, oper, code, mask)
                     if verbose > 0 then
                         print("### CODE UPDATE: mask: " .. tostring(mask))
                         if verbose > 1 then
                             print(code)
                         end
                     end
                     local msg = m:JSON({ command = 1,
                                          appid = "org.tizen.ico.homescreen",
                                          arg = m:JSON({ device = code.device,
                                                         time = code.time,
                                                         input = code.input,
                                                         code = code.id,
                                                         state = code.state
                                           })
                     })
                     if verbose > 0 then
                         print("### <== sending " ..
                               command_name(msg.command) ..
                               " input message")
                         if verbose > 1 then
                             print(msg)
                         end
                     end
                     sc:send_message(homescreen, msg)
                 end
}

sc = m:get_system_controller()

-- resource sets
sets = {}

-- special screen resource sets
-- TODO: just rewrite screen resource handling to use regular resource API

special_screen_sets = {}

-- user manager
um = m:UserManager()

connected = false
homescreen = ""
onscreen = ""

cids = {}

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
    windows         = { {'ico_hs_controlbarwindow', 'Center.Control'} },
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

application {
    appid           = "org.tizen.ico.onscreen",
    area            = "Center.Full",
    privileges      = { screen = "system", audio = "system" },
    resource_class  = "player",
    screen_priority = 20
}

application {
    appid           = "org.tizen.ico.login",
    area            = "Center.Full",
    privileges      = { screen = "system", audio = "system" },
    resource_class  = "player",
    screen_priority = 20
}

application {
    appid           = "org.tizen.ico.camera_left",
    area            = "Center.SysApp.Left",
    privileges      = { screen = "system", audio = "none" },
    requisites      = { screen = "blinker_left", audio = "none" },
    resource_class  = "player",
    screen_priority = 30
}

application {
    appid           = "org.tizen.ico.camera_right",
    area            = "Center.SysApp.Right",
    privileges      = { screen = "system", audio = "none" },
    requisites      = { screen = "blinker_right", audio = "none" },
    resource_class  = "player",
    screen_priority = 30
}

application {
    appid           = "net.zmap.navi",
    area            = "Center.Full",
    privileges      = { screen = "none", audio = "none" },
    resource_class  = "navigator",
    screen_priority = 30
}

application {
    appid           = "GV3ySIINq7.GhostCluster",
    area            = "Center.Full",
    privileges      = { screen = "none", audio = "none" },
    resource_class  = "system",
    requisites      = { screen = "driving", audio = "none" },
    screen_priority = 30
}

application {
    appid           = "MediaPlayer",
    area            = "Center.Full",
    privileges      = { screen = "none", audio = "none" },
    requisites      = { screen = "driving", audio = "none" },
    resource_class  = "player",
    screen_priority = 0
}

application {
    appid           = "MyMediaPlayer",
    area            = "Center.Full",
    privileges      = { screen = "none", audio = "none" },
    requisites      = { screen = "driving", audio = "none" },
    resource_class  = "player",
    screen_priority = 0
}

application {
    appid           = "MeterWidget",
    area            = "Center.Full",
    privileges      = { screen = "none", audio = "none" },
    requisites      = { screen = "driving", audio = "none" },
    resource_class  = "player",
    screen_priority = 0
}

application {
    appid           = "org.tizen.ico.app-soundsample",
    area            = "Center.Full",
    privileges      = { screen = "none", audio = "none" },
    -- uncomment the next line to make the app exempt from regulation
    -- requisites      = { screen = "driving", audio = "none" },
    resource_class  = "player",
    screen_priority = 0
}


if sc then
    sc.client_handler = function (self, cid, msg)
        local command = msg.command
        local appid = msg.appid
        if verbose > 0 then
            print('### ==> client handler:')
            if verbose > 1 then
                print(msg)
            end
        end

        -- known commands: 1 for SEND_APPID, synthetic command 0xFFFF for
        -- disconnection

        if command == 0xFFFF then
            if verbose > 1 then
                print('client ' .. cid .. ' disconnected')
            end
            if msg.appid == homescreen then
                homescreen = ""
                for i,v in pairs(special_screen_sets) do
                    resclnt:resource_set_destroy("screen", i)
                    special_screen_sets[i] = nil
                end
            end
            return
        end

        -- handle the connection to weston

        if appid then
            if appid == "org.tizen.ico.homescreen" then
                print('Setting homescreen='..appid)
                homescreen = appid
                if command and command == 1 then
                    send_driving_mode_to(homescreen)
                    send_night_mode_to(homescreen)
                end
            elseif appid == "org.tizen.ico.onscreen" then
                onscreen = appid
                if command and command == 1 then
                    send_driving_mode_to(onscreen)
                    send_night_mode_to(onscreen)
                end
            end

        if not connected and appid == "org.tizen.ico.homescreen" then
                print('Trying to connect to weston...')
                connected = wmgr:connect()
            end
            cids[cid] = appid
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
                   command_name(msg.command) .. ' message from ' .. cids[cid])
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

            local surface        = msg.arg.surface
            local system_surface = false
            local appdb          = wmgr:application(msg.appid)
            system_surface       = appdb.privileges.screen == "system"

            msg.arg.visible = 1

            if msg.arg then
                local time = 200
                if  msg.arg.anim_time then
                    local t = msg.arg.anim_time
                    -- the actual time for the animation
                    time = m:AND(t, time_mask)
                    -- flag for ignoring resource control
                    nores = m:AND(t, nores_mask)
                    if m:AND(t, raise_mask) then
                        msg.arg.raise = 1
                    elseif m:AND(t, lower_mask) then
                        msg.arg.raise = 0
                    end
                end
                if msg.arg.anim_name then
                    a.show = { msg.arg.anim_name, time }
                    print('time: ' .. tostring(time))
                end
            end

            -- all show messages from system surfaces are forced
            if not nores and system_surface then
                nores = true
                if not msg.arg.raise then
                    msg.arg.raise = 1
                end
            end

            if verbose > 2 then
                print('### ==> SHOW')
                print(tostring(msg.arg) .. ", system_surface=" .. tostring(system_surface))
            end

            if nores then
                wmgr:window_request(msg.arg, a, 0)

                -- only non-system surfaces have resource sets
                if not system_surface then
                    resclnt:resource_set_acquire("screen", surface)
                end
            else
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
                    -- the actual time for the animation
                    time = m:AND(t, time_mask)
                    -- flag for ignoring resource control
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
                    msg.arg.raise = 0
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
            if msg.arg.zone then
                msg.arg.area = msg.arg.zone
            end
            wmgr:window_request(msg.arg, a, 0)
            -- TODO: handle if area changed
        elseif msg.command == 0x10007 then   -- ico CHANGE_ACTIVE
            if not msg.arg.active then
                msg.arg.active = 3 -- pointer + keyboard
            end
            if verbose > 2 then
                print('### ==> CHANGE_ACTIVE REQUEST')
                print(tostring(msg.arg))
            end
            wmgr:window_request(msg.arg, a, 0)
        elseif msg.command == 0x10008 then   -- ico CHANGE_LAYER
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
        elseif msg.command == 0x10020 then   -- ico MAP_THUMB
            local framerate = msg.arg.framerate
            local animname  = msg.arg.anim_name
            if animname then
               a.map = { animname, 1 }
               if not framerate or framerate < 0 then
                  framerate = 5
               end
               msg.arg.mapped = 1
               if verbose > 2 then
                  print('### ==> MAP_THUMB REQUEST')
                  print(msg.arg)
                  print('framerate: '..framerate)
               end
               wmgr:window_request(msg.arg, a, framerate)
            end
        elseif msg.command == 0x10021 then   -- ico UNMAP_THUMB
            msg.arg.mapped = 0
            if verbose > 2 then
                print('### ==> UNMAP_THUMB REQUEST')
                print(msg.arg)
            end
            wmgr:window_request(msg.arg, a, 0)
--[[
        elseif msg.command == 0x10013 then -- ico MAP_BUFFER command
            local shmname = msg.arg.anim_name
            local bufsize = msg.arg.width
            local bufnum  = msg.arg.height
            if shmname and bufsize and bufnum then
                if verbose > 2 then
                    print('### ==> MAP_BUFFER REQUEST')
                    print("shmaname='" .. shmname ..
                          "' bufsize='" .. bufsize ..
                          " bufnum=" .. bufnum)
                end
                wmgr:buffer_request(shmname, bufsize, bufnum)
            end
--]]
        elseif msg.command == 0x10030 then   -- ico SHOW_LAYER command
            msg.arg.visible = 1
            if verbose > 2 then
                print('### ==> SHOW_LAYER REQUEST')
                print(msg.arg)
            end
            wmgr:layer_request(msg.arg)
        elseif msg.command == 0x10031 then   -- ico HIDE_LAYER command
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
            print('### ==> input handler: ' .. command_name(msg.command))
            if verbose > 1 then
                print(msg)
            end
        end
        if msg.command == 0x20001 then -- add_input
            msg.arg.appid = msg.appid
            if verbose > 2 then
                print('### ==> ADD_INPUT REQUEST')
                print(tostring(msg.arg))
            end
            imgr:input_request(msg.arg)
        elseif msg.command == 0x20002 then -- del_input
            msg.arg.appid = ''
            if verbose > 2 then
                print('### ==> DEL_INPUT REQUEST')
                print(tostring(msg.arg))
            end
            imgr:input_request(msg.arg)
        -- elseif msg.command == 0x20003 then -- send_input
        end
    end

    sc.user_handler = function (self, cid, msg)
        if verbose > 0 then
            print('### ==> user handler: ' .. command_name(msg.command))
            if verbose > 1 then
                print(msg)
            end
        end

        if not um then
            print("User Manager not initialized")
            return
        end

        if msg.command == 0x30001 then -- change_user
            print("command CHANGE_USER")
            if not msg.arg then
                print("invalid message")
                return
            end

            username = msg.arg.user
            passwd = msg.arg.pass

            if not username then
                username = ""
            end

            if not passwd then
                passwd = ""
            end

            success = um:changeUser(username, passwd)

            if not success then
                reply = m.JSON({
                    appid = msg.appid,
                    command = cmd
                })
                if sc:send_message(msg.appid, reply) then
                    print('*** sent authentication failed message')
                else
                    print('*** failed to send authentication failed message')
                end
            end

        elseif msg.command == 0x30002 then -- get_userlist
            print("command GET_USERLIST")
            if not msg.appid then
                print("invalid message")
                return
            end

            users, currentUser = um:getUserList()

            if not users then
                print("failed to get user list")
                return
            end

            nUsers = 0

            for i,v in pairs(users) do
                nUsers = nUsers + 1
            end

            if not currentUser then
                currentUser = ""
            end

            if verbose > 1 then
                print("current user: " .. currentUser)
                print("user list:")
                for i,v in pairs(users) do
                    print(v)
                end
            end

            reply = m.JSON({
                appid = msg.appid,
                command = cmd,
                arg = m.JSON({
                    user_num = nUsers,
                    user_list = users,
                    user_login = currentUser
                })
            })

            if verbose > 1 then
                print("### <== GetUserList reply: " .. tostring(reply))
            end

            if sc:send_message(msg.appid, reply) then
                print('*** reply OK')
            else
                print('*** reply FAILED')
            end

        elseif msg.command == 0x30003 then -- get_lastinfo
            print("command GET_LASTINFO")
            if not msg.appid then
                print("invalid message")
                return
            end

            lastInfo = um:getLastInfo(msg.appid)

            if not lastInfo then
                print("failed to get last info for app" .. msg.appid)
                return
            end

            reply = m.JSON({
                appid = msg.appid,
                command = cmd,
                arg = m.JSON({
                    lastinfo = lastinfo
                })
            })

            if sc:send_message(msg.appid, reply) then
                print('*** reply OK')
            else
                print('*** reply FAILED')
            end

        elseif msg.command == 0x30004 then -- set_lastinfo
            print("command SET_LASTINFO")
            if not msg.arg or not msg.appid then
                print("invalid message")
                return
            end

            lastInfo = um:setLastInfo(msg.appid, msg.arg.lastinfo)
        end
    end

    sc.resource_handler = function (self, cid, msg)
        if verbose > 0 then
            print('### ==> resource handler: ' .. command_name(msg.command))
            if verbose > 1 then
                print(msg)
            end
        end

        getKey = function (msg)
            -- Field rset.key is an id for distinguishing between rsets. All
            -- resource types appear to contain a different key. Just pick one
            -- based on what we have on the message.

            key = nil

            if msg and msg.res then
                if msg.res.sound then
                    key = msg.res.sound.id
                elseif msg.res.input then
                    key = msg.res.input.name
                elseif msg.res.window then
                    -- alarm! this appars to be non-unique?
                    key = msg.res.window.resourceId
                end
            end

            return key
        end

        createResourceSet = function (ctl, client, msg)
            cb = function(rset)

                -- m:info("*** resource_cb: client = '" .. msg.appid .. "'")

                -- type is either basic (0) or interrupt (1)
                requestType = 0
                if msg.res.type then
                    requestType = msg.res.type
                end

                if rset.data.filter_first then
                    rset.data.filter_first = false
                    return
                end

                if rset.acquired then
                    cmd = 0x00040001 -- acquire

                    if msg.appid == onscreen then
                        -- notifications are valid only for a number of seconds
                        rset.timer = m:Timer({
                            interval = 5000,
                            oneshot = true,
                            callback = function (t)
                                m:info("notification timer expired")

                                if rset.data then
                                    reply.res.window = rset.data.window
                                end

                                rset:release()
                                rset.timer = nil

                                -- Send a "RELEASE" message to client.
                                -- This triggers the resource deletion
                                -- cycle in OnScreen.

                                reply = m.JSON({
                                    appid = msg.appid,
                                    command = cmd,
                                    res = {
                                        type = requestType
                                    }
                                })

                                sc:send_message(client, reply)
                            end
                        })
                    end
                else
                    cmd = 0x00040002 -- release
                    if rset.timer then
                       rset.timer.callback = nil
                       rset.timer = nil
                    end
                end

                reply = m.JSON({
                        appid = msg.appid,
                        command = cmd,
                        res = {
                            type = requestType
                        }
                    })

                if rset.data.sound then
                    reply.res.sound = rset.data.sound
                end

                if rset.data.window then
                    reply.res.window = rset.data.window
                end

                if rset.data.input then
                    reply.res.input = rset.data.input
                end

                if rset.acquired then
                    m:info("resource cb: 'acquire' reply to client " .. client)
                else
                    m:info("resource cb: 'release' reply to client " .. client)
                end

                if not sc:send_message(client, reply) then
                    m:info('*** reply FAILED')
                end
            end

            local rset = m:ResourceSet({
                    application_class = "player",
                    zone = "driver", -- msg.zone ("full")
                    callback = cb
                })

            rset.data = {
                cid = cid,
                ctl = ctl,
                filter_first = true
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

                rset.data.sound = msg.res.sound
            end

            if msg.res.input then
                rset:addResource({
                        resource_name = "input"
                    })
                rset.resources.input.attributes.pid = tostring(msg.pid)
                rset.resources.input.attributes.appid = msg.appid
                print("input name: " .. msg.res.input.name)
                print("input event:" .. tostring(msg.res.input.event))

                rset.data.input = msg.res.input
            end

            if msg.res.window then
                rset:addResource({
                        resource_name = "screen",
                        shared = true
                    })
                rset.resources.screen.attributes.pid = tostring(msg.pid)
                rset.resources.screen.attributes.appid = msg.appid
                rset.resources.screen.attributes.surface = msg.res.window.resourceId
                complete_area = msg.res.window.display .. '.' .. msg.res.window.area
                rset.resources.screen.attributes.area = complete_area
                if msg.appid == onscreen then
                    rset.resources.screen.attributes.classpri = 1
                else
                    rset.resources.screen.attributes.classpri = 0
                end

                rset.data.window = msg.res.window
            end

            rset.key = getKey(msg)

            return rset
        end

        -- parse the message

        -- fields common to all messages:
        --      msg.command
        --      msg.appid
        --      msg.pid

        if msg.command == 0x40011 then -- create_res
            print("command CREATE_RES")
            key = getKey(msg)

            if key then
                if not sets.cid then
                    sets.cid = {}
                end
                sets.cid[key] = createResourceSet(self, cid, msg)
            end

        elseif msg.command == 0x40012 then -- destroy_res
            print("command DESTROY_RES")
            key = getKey(msg)

            if key then
                if sets.cid and sets.cid[key] then
                    sets.cid[key]:release()
                    sets.cid[key].timer = nil
                    sets.cid[key] = nil -- garbage collecting
                end
            end

        elseif msg.command == 0x40001 then -- acquire_res
            print("command ACQUIRE_RES")
            key = getKey(msg)

            if key then
                if not sets.cid then
                    sets.cid = {}
                end
                if not sets.cid[key] then
                    sets.cid[key] = createResourceSet(self, cid, msg)
                end
                print("acquiring the resource set")
                sets.cid[key]:acquire()
            end

        elseif msg.command == 0x40002 then -- release_res
            print("command RELEASE_RES")

            key = getKey(msg)

            if key then
                if sets.cid and sets.cid[key] then
                    sets.cid[key]:release()

                    if msg.appid == onscreen then
                        -- in case of OnScreen, this actually means that the
                        -- resource set is never used again; let gc do its job
                        sets.cid[key].timer = nil
                        sets.cid[key] = nil -- garbage collecting
                    end
                end
            end

        elseif msg.command == 0x40003 then -- deprive_res
            print("command DEPRIVE_RES")

        elseif msg.command == 0x40004 then -- waiting_res
            print("command WAITING_RES")

        elseif msg.command == 0x40005 then -- revert_res
            print("command REVERT_RES")
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

    sc.notify_handler = function (self, cid, msg)
        if verbose > 0 then
            print('### notify handler: ' .. command_name(msg.command))
            if verbose > 1 then
                print(msg)
            end
        end
    end
end

function send_driving_mode_to(client)
    if client == "" then
        return
    end

    local driving_mode = mdb.select.select_driving_mode.single_value

    if not driving_mode then driving_mode = 0 end

    local reply = m:JSON({ command = 0x60001,
                           arg     = m:JSON({ stateid = 1,
                                              state   = driving_mode
                                     })
                  })

    if verbose > 0 then
        print("### <== sending " .. command_name(reply.command) .. " message")
        if verbose > 1 then
            print(reply)
        end
    end

    sc:send_message(client, reply)
end

function send_night_mode_to(client)
    if client == "" then
        return
    end

    local night_mode = mdb.select.select_night_mode.single_value

    if not night_mode then night_mode = 0 end

    local reply = m:JSON({ command = 0x60001,
                           arg     = m:JSON({ stateid = 2,
                                              state   = night_mode
                                     })
                  })

    if verbose > 0 then
        print("### <== sending " .. command_name(reply.command) .. " message")
        if verbose > 1 then
            print(reply)
        end
     end

     sc:send_message(client, reply)
end

-- we should have 'audio_playback' defined by now
m:try_load_plugin('telephony')
