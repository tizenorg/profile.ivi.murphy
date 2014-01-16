--[[
    Vehicle speed property

    This property has a basic type which is updated often, therefore use
    the built-in handler.
--]]

amb.property {
    name = "vehicle_speed",
    basic_table_name = "amb_vehicle_speed",
    dbus_data = {
        obj = "undefined",
        interface = "org.automotive.VehicleSpeed",
        property = "VehicleSpeed",
        signature = "q",
    },
}


--[[
    Gear position property
--]]

amb.property {
    name = "gear_position",
    basic_table_name = "amb_gear_position",
    dbus_data = {
        obj = "undefined",
        interface = "org.automotive.Transmission",
        objectname = "Transmission",
        property = "GearPosition",
        signature = "i",
    },
}


--[[
    Exterior brightness property
--]]

amb.property {
    name = "exterior_brightness",
    basic_table_name = "amb_exterior_brightness",
    dbus_data = {
        obj = "undefined",
        interface = "org.automotive.ExteriorBrightness",
        property = "ExteriorBrightness",
        signature = "q",
    },
}

--[[
    Turn signal property
--]]

amb.property {
    name = "turn_signal",
    basic_table_name = "amb_turn_signal",
    dbus_data = {
        obj = "undefined",
        interface = "org.automotive.TurnSignal",
        property = "TurnSignal",
        signature = "i",
    },
}
