add_rules("mode.debug", "mode.check", "mode.release")

option("tests", {default = false, description = "Build tests programs"})

set_languages("c++23", { public = true })
set_warnings("all", "extra", "pedantic", "error", {private=true})

add_repositories("ddst-xrepo https://github.com/SJTU-DDST/xmake-repo.git")
add_requires("rdmapp 0.1.0", {
    public=true,
    configs={examples=false, asio_coro=false, nortti=false, enable_pic=true}
})
add_requires("cppcoro-20", {public=true})
add_requires("concurrentqueue 1.0.4", {private=true})
add_requires("spdlog 1.16.0", {private=true, configs={header_only=true}})
add_requires("glaze 7.0.0", {public=true})


target("coverbs-rpc")
    add_headerfiles("include/(coverbs_rpc/**.hpp)")
    add_includedirs("include", {public=true})
    set_kind("static")
    if has_config("pic") then
        add_cxxflags("-fpic")
    end
    add_packages("rdmapp",  {public=true})
    add_packages("cppcoro-20", {public=true})
    add_packages("glaze", {public=true})
    add_packages("spdlog", {private=true})
    add_packages("concurrentqueue", {private=true})
    add_files("src/conn/*.cc")
    add_files("src/*.cc")

if has_config("tests") then
    rule("test_config")
        on_load(function (target)
            target:add("includedirs", "tests/include")
            target:set("kind", "binary")
            target:add("deps", "coverbs-rpc")
            target:add("packages", "spdlog")
        end)

    target("naive_test")
        add_files("tests/naive_test.cc")
        add_rules("test_config")

    target("spin_wait_test")
        add_files("tests/spin_wait_test.cc")
        add_rules("test_config")

    target("basic_conn_test")
        add_files("tests/basic_conn_test.cc")
        add_rules("test_config")

    target("basic_rpc_test_server")
        add_files("tests/basic_rpc_test_server.cc")
        add_rules("test_config")

    target("basic_rpc_test_client")
        add_files("tests/basic_rpc_test_client.cc")
        add_rules("test_config")

    target("basic_rpc_mux_test_server")
        add_files("tests/basic_rpc_mux_test_server.cc")
        add_rules("test_config")

    target("basic_rpc_mux_test_client")
        add_files("tests/basic_rpc_mux_test_client.cc")
        add_rules("test_config")

    target("typed_rpc_basic_test")
        add_files("tests/typed_rpc_basic_test.cc")
        add_rules("test_config")

    target("typed_rpc_mux_test_server")
        add_files("tests/typed_rpc_mux_test_server.cc")
        add_rules("test_config")

    target("typed_rpc_mux_test_client")
        add_files("tests/typed_rpc_mux_test_client.cc")
        add_rules("test_config")

    target("typed_rpc_benchmark_client")
        add_files("tests/typed_rpc_benchmark_client.cc")
        add_rules("test_config")

    target("typed_rpc_benchmark_server")
        add_files("tests/typed_rpc_benchmark_server.cc")
        add_rules("test_config")
end