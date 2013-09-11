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
        share_mmplayer = "player:AVP,mandatory,exclusive,relaxed"
    })
else
    m:info("No audio session manager plugin found...")
end

if m:plugin_exists('ivi-resource-manager.disabled') then
    m:load_plugin('ivi-resource-manager')
end

-- define application classes
application_class { name="interrupt", priority=99, modal=true , share=false, order="fifo" }
application_class { name="emergency", priority=80, modal=false, share=false, order="fifo" }
application_class { name="alert"    , priority=51, modal=false, share=false, order="fifo" }
application_class { name="navigator", priority=50, modal=false, share=true , order="fifo" }
application_class { name="phone"    , priority=6 , modal=false, share=true , order="lifo" }
application_class { name="camera"   , priority=5 , modal=false, share=false, order="lifo" }
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
if not m:plugin_exists('ivi-resource-manager.disabled') then
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

if not m:plugin_exists('ivi-resource-manager.disabled') then
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

mdb.table {
    name = "amb_nightmode",
    index = { "id" },
    create = true,
    columns = {
        { "id", mdb.unsigned },
        { "night_mode", mdb.unsigned }
    }
}

element.lua {
    name    = "nightmode",
    inputs  = { brightness = mdb.select.exterior_brightness },
    oldmode = -1;
    outputs = {
    mdb.table {
        name = "mandatory_placeholder_to_prevent_spurious_updates",
            create = true,
            columns = { { "id", mdb.unsigned } }
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

mdb.table {
    name = "amb_drivingmode",
    index = { "id" },
    create = true,
    columns = {
        { "id", mdb.unsigned },
        { "driving_mode", mdb.unsigned }
    }
}

element.lua {
    name    = "drivingmode",
    inputs  = { speed = mdb.select.vehicle_speed },
    oldmode = -1;
    outputs = {
    mdb.table {
        name = "another_mandatory_placeholder_to_prevent_spurious_updates",
            create = true,
            columns = { { "id", mdb.unsigned } }
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

-- load the telephony plugin
m:try_load_plugin('telephony')
