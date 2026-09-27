__attribute__((visibility("default")))
int fixture_app_child_value(void);

__attribute__((visibility("default"), noinline))
int fixture_app_root_call(void) {
    return fixture_app_child_value();
}
