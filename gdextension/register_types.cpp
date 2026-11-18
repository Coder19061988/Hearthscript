// Entry point of the Kindlescript GDExtension (see godot/kindlescript.gdextension).

#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "kindle_simulation.h"

using namespace godot;

namespace {

void initialize_kindlescript(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(kindle_godot::KindleSimulation);
}

void uninitialize_kindlescript(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

}  // namespace

extern "C" {
GDExtensionBool GDE_EXPORT kindlescript_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                     GDExtensionClassLibraryPtr library,
                                                     GDExtensionInitialization* initialization) {
    GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_kindlescript);
    init.register_terminator(uninitialize_kindlescript);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
}
