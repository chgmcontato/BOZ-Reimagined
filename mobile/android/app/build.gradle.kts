import java.net.URI
import java.security.MessageDigest

plugins {
    id("com.android.application")
}

// ---- Native dependencies ----
// SDL 2 (its Java classes go into the app, its C library is built by mobile/native/CMakeLists.txt)
// and the Boost headers Dynarmic needs. Downloaded once into mobile/android/.deps (git-ignored)
// and checked against a fixed SHA-256, during configuration so an IDE sync finds them too.
data class Dep(val url: String, val sha256: String, val dir: String, val include: String)

val depsDir: File = rootProject.file(".deps")
val sdl2 = Dep(
    "https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz",
    "24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4",
    "SDL2-2.30.9", "SDL2-2.30.9/**")
val boost = Dep(
    "https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-b2-nodocs.tar.gz",
    "2128a4c96862b5c0970c1e34d76b1d57e4a1016b80df85ad39667f30b1deba26",
    "boost-1.86.0", "boost-1.86.0/boost/**")

fun sha256(file: File): String {
    val md = MessageDigest.getInstance("SHA-256")
    file.inputStream().use { input ->
        val buffer = ByteArray(1 shl 16)
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            md.update(buffer, 0, n)
        }
    }
    return md.digest().joinToString("") { "%02x".format(it) }
}

fun ensure(dep: Dep): File {
    val out = File(depsDir, dep.dir)
    val stamp = File(out, ".boz-ok")
    if (stamp.isFile && stamp.readText() == dep.sha256) return out
    depsDir.mkdirs()
    val archive = File(depsDir, dep.url.substringAfterLast('/'))
    if (!archive.isFile || sha256(archive) != dep.sha256) {
        logger.lifecycle("BOZ: downloading ${dep.url}")
        URI(dep.url).toURL().openStream().use { input ->
            archive.outputStream().use { input.copyTo(it) }
        }
        val got = sha256(archive)
        if (got != dep.sha256) {
            archive.delete()
            throw GradleException("SHA-256 mismatch for ${archive.name}: $got, expected ${dep.sha256}")
        }
    }
    logger.lifecycle("BOZ: extracting ${archive.name}")
    copy {
        from(tarTree(resources.gzip(archive))) { include(dep.include) }
        into(depsDir)
    }
    stamp.writeText(dep.sha256)
    return out
}

val sdl2Dir = ensure(sdl2)
val boostDir = ensure(boost)

// The launcher icon is the PC's (repository root), copied into generated resources.
val iconRes = layout.buildDirectory.dir("generated/boz-res").get().asFile
run {
    val icon = rootProject.file("../../call-of-duty-black-ops-zombies-icon.png")
    val target = File(iconRes, "mipmap-xxxhdpi/ic_launcher.png")
    if (icon.isFile && (!target.isFile || target.lastModified() < icon.lastModified())) {
        target.parentFile.mkdirs()
        icon.copyTo(target, overwrite = true)
    }
}

android {
    namespace = "com.carlos.boz.mobile"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        // Not com.activision.boz: installs next to the original game and the TV build.
        applicationId = "com.carlos.boz.mobile"
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = "0.2-poc"
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DBOZ_SDL2_SOURCE_DIR=${sdl2Dir.absolutePath}",
                    "-DBOZ_BOOST_INCLUDE_DIR=${boostDir.absolutePath}",
                    // two C++ libraries in the app (SDL's hidapi, Dynarmic): one shared libc++,
                    // as the NDK recommends; AGP packages libc++_shared.so
                    "-DANDROID_STL=c++_shared",
                    // ELF segments aligned for 16 KiB-page devices too (Android 15+)
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../native/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].java.srcDir(File(sdl2Dir, "android-project/app/src/main/java"))
    sourceSets["main"].res.srcDir(iconRes)

    // A fixed test key keeps updates installable over the previous build, so the imported files
    // survive. It comes from the environment (repository secrets in CI), never from the repository.
    // Without it the debug key is used, which differs from machine to machine.
    val testKeystore = System.getenv("BOZ_TEST_KEYSTORE")
    val testPassword = System.getenv("BOZ_TEST_KEYSTORE_PASSWORD")
    signingConfigs {
        if (!testKeystore.isNullOrEmpty() && !testPassword.isNullOrEmpty()) {
            create("bozTest") {
                storeFile = file(testKeystore)
                storePassword = testPassword
                keyAlias = "boz-mobile-test"
                keyPassword = testPassword
            }
        }
    }

    buildTypes {
        // The JIT and the runtime are slow at -O0: test with assembleRelease (RelWithDebInfo).
        // Not for distribution: either key below is a development key.
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("bozTest") ?: signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            // uncompressed and page-aligned, loaded straight from the APK (AGP default for minSdk 23+)
            useLegacyPackaging = false
            keepDebugSymbols += "**/*.so" // readable native crash dumps from the PoC
        }
    }
}
