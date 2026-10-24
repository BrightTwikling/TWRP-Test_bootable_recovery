package twrp

import (
	"android/soong/android"
	"android/soong/cc"
	"path/filepath"
)

func gui2Resources(ctx android.LoadHookContext) {
	root := getRecoveryAbsDir(ctx)
	dest := filepath.Join(ctx.Config().Getenv("OUT"), "recovery/root/twres")
	if err := copyDir(root+"gui2/resources/fonts", filepath.Join(dest, "fonts")); err != nil {
		ctx.ModuleErrorf("GUI2 fonts: %v", err)
	}
}
func init() {
	android.RegisterModuleType("gui2_resources_defaults", func() android.Module {
		module := cc.DefaultsFactory()
		android.AddLoadHook(module, gui2Resources)
		return module
	})
}
