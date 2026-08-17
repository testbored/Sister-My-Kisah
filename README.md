# SRE Studio

SRE Studio adalah framework decompiler desktop berbasis **C++17** untuk binary **ELF 64-bit, x86-64, little-endian**. Proyek menargetkan hasil kompilasi C++ sederhana dengan ABI **System V AMD64** dan optimasi `-O1`.

Tujuannya bukan menghasilkan source yang identik byte-per-byte, melainkan memulihkan fungsi, alur kontrol, operasi aritmetika, pemanggilan fungsi, serta variabel utama menjadi pseudocode C++ yang mudah dibaca.

> Batasan: exception, template kompleks, multiple inheritance, virtual function, RTTI, binary ter-obfuscate, dan pola optimasi `-O2`/`-O3` bukan target utama.

## Target binary

Kompilasikan source yang akan dianalisis dengan konfigurasi berikut:

```bash
g++ zeta.cpp -O1 -fno-inline -fno-omit-frame-pointer -no-pie -o zeta
```

`-fno-omit-frame-pointer` membuat akses variabel lokal melalui `rbp` lebih mudah dipulihkan. `-fno-inline` mempertahankan batas antar fungsi. Jangan gunakan `strip`, karena daftar fungsi dibaca dari symbol table.

## Build dan menjalankan aplikasi

### Dependensi

Pada Ubuntu/Debian:

```bash
sudo apt install build-essential cmake libx11-dev binutils
```

Komponen yang diperlukan adalah CMake, compiler C++, header X11, dan GNU binutils (`nm` serta `objdump`).

### Build

```bash
cmake -S . -B build
cmake --build build
```

Executable berada di `build/sre_studio`.

### Run

Berikan binary ELF melalui argumen:

```bash
./build/sre_studio ./zeta
```

Atau jalankan tanpa argumen, masukkan path binary pada kolom atas, lalu tekan **ANALYZE**:

```bash
./build/sre_studio
```

### Interaksi GUI

| Aksi | Cara memakai |
| --- | --- |
| Memilih fungsi | Klik nama fungsi pada sidebar. |
| Quick search | Setelah binary dianalisis, ketik nama fungsi; daftar langsung difilter. Backspace menghapus filter. |
| Melihat C++/assembly | Klik fungsi pada sidebar. Panel kiri menunjukkan pseudocode, panel kanan menunjukkan assembly beralamat dan nomor baris. |
| Membuka Call Graph | Tekan `G`. |
| Zoom graph | Putar scroll mouse. |
| Pan graph | Drag area kosong pada canvas graph. |
| Membuka fungsi dari graph | Klik node fungsi. |
| Keluar | Tekan `Esc`. |

## Alur proses decompiler

```text
ELF input
  │
  ├─ 1. Validasi header ELF
  │     magic ELF, class 64-bit, little-endian, machine x86-64
  ├─ 2. Ambil daftar fungsi
  │     nm -n -S --defined-only --demangle
  ├─ 3. Disassembly per fungsi
  │     objdump -d -Mintel --demangle --no-show-raw-insn
  ├─ 4. Analisis heuristik
  │     register/ABI, stack frame, compare, branch, back-edge, call, return
  ├─ 5. Rekonstruksi pseudocode C++
  └─ 6. Presentasi GUI
        daftar fungsi, assembly, C++, dan call graph
```

### 1. Validasi ELF

`ElfAnalyzer::isSupportedElf()` membaca header langsung dari binary. File hanya diterima jika memiliki magic `\x7FELF`, class ELF64, data little-endian, dan machine `EM_X86_64`. Validasi awal ini mencegah analisis terhadap format atau arsitektur yang salah.

### 2. Recovery fungsi dan assembly

`nm` memperoleh address, ukuran, dan nama fungsi. Nama di-demangle agar nama C++ seperti `result(int, int)` dapat ditampilkan baik. `objdump` menghasilkan instruksi Intel syntax; instruksi tersebut dibagi dan disimpan sebagai `Instruction` di dalam `Function`.

### 3. Recovery nilai dan variabel

Decompiler memakai pelacakan nilai sederhana (*symbolic value tracking*):

- Register parameter System V AMD64 dipetakan ke `arg1`–`arg6` (`rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`).
- `rax` dipetakan ke nilai return.
- Alias 32-bit seperti `eax`, `edi`, dan `esi` dinormalisasi ke register 64-bitnya.
- Operand `[rbp-...]` dipulihkan sebagai variabel lokal `local_...`.
- `mov`, `lea`, `add`, `sub`, `imul`, `xor`, `cmp`, dan `test` diterjemahkan menjadi assignment, ekspresi, atau kondisi C++.

Pendekatan ini dipilih karena binary target memakai frame pointer dan optimasi rendah. Implementasinya ringkas, dapat dibaca, dan cukup kuat untuk tugas dasar tanpa framework disassembler besar.

### 4. Recovery `if`, `if-else`, dan loop

Instruksi `cmp`/`test` menyimpan dua ekspresi terakhir yang dibandingkan. Conditional jump (`je`, `jne`, `jg`, `jle`, dan lainnya) memakai ekspresi tersebut untuk membuat kondisi C++.

Untuk loop, decompiler mencari **back-edge**, yaitu conditional jump menuju address instruksi yang lebih kecil. Ini pola umum loop hasil kompilasi x86-64. Guard yang melompat keluar dari loop, diikuti oleh back-edge, diterjemahkan menjadi:

```cpp
if (condition) {
    do {
        // body loop
    } while (condition_loop);
}
```

Pola ini mencakup `for` sederhana dan `while` yang diturunkan compiler menjadi compare/jump. Pada binary, bentuk source `for` dan `while` tidak selalu dapat dibedakan sempurna karena keduanya dapat menghasilkan control-flow graph yang sama. SRE Studio memilih bentuk yang paling jelas untuk alur tersebut.

`if-else` sederhana dideteksi melalui conditional branch dan jump menuju blok akhir. Untuk nested branch atau CFG yang rumit, aplikasi tetap mempertahankan condition dan address target sebagai komentar agar alur tidak hilang.

## Strategi implementasi

| Masalah | Pendekatan | Alasan |
| --- | --- | --- |
| Membaca ELF tanpa library besar | Validasi header manual + `nm`/`objdump`. | GNU binutils stabil, tersedia di Linux, dan memungkinkan fokus pada dekompilasi. |
| Memulihkan tipe/variabel | Symbolic tracking register dan slot `rbp`. | Cocok dengan ABI System V dan `-fno-omit-frame-pointer`; lebih mudah dipahami daripada SSA penuh. |
| Memulihkan struktur kontrol | Pencocokan `cmp/test`, conditional jump, dan back-edge. | Loop dan branch pada kode `-O1` masih memiliki pola konsisten. |
| Menjaga hasil tetap jujur | Pseudocode heuristik + komentar address target. | Decompiler tidak mengklaim struktur yang tidak dapat dibuktikan dari binary. |
| GUI portable tanpa framework besar | C++ + X11 native. | Tidak bergantung pada Qt/GTK dan cukup untuk desktop Linux. |

## Generated test case dan hasil

File [test.cpp](test.cpp) adalah test case yang disertakan:

```cpp
int result(int a, int b) {
    if (a > 0) {
        for (int i = 1; i <= a; i++) {
            b = b * i;
        }
    }
    return b;
}
```

Build test dan buka dengan SRE Studio:

```bash
g++ test.cpp -O1 -fno-inline -fno-omit-frame-pointer -no-pie -o test
./build/sre_studio ./test
```

Pola assembly inti yang dihasilkan berupa guard `jle`, operasi `imul`, update counter, lalu `cmp` dan `jle` kembali ke awal loop. SRE Studio mengenal jump kembali itu sebagai back-edge dan merekonstruksi bentuk yang setara secara logika:

```cpp
long result(long arg1, long arg2, ...) {
    if (arg1 > 0) {
        long i = 1;
        do {
            arg2 *= i;
            i += 1;
        } while (i <= arg1);
    }
    return arg2;
}
```

Versi awal test menggunakan `for (int i = a; i < a; i++)`. Kondisi itu selalu salah, sehingga compiler `-O1` menghapus loop dan `if` seluruhnya. Decompiler tidak dapat memulihkan loop yang sudah tidak ada di binary; test saat ini memakai kondisi valid agar pola control-flow tersedia.

## Bonus: Call Graph

Call graph dibangun dari setiap instruksi `call` yang mempunyai target bernama. Nama target dibandingkan dengan tabel fungsi internal untuk membentuk edge `caller → callee`.

Saat `G` ditekan, aplikasi menampilkan canvas graph dengan fitur berikut:

- Node berisi nama fungsi dan address; fungsi aktif diberi warna khusus.
- Edge berpanah menunjukkan arah pemanggilan fungsi.
- Layout berlapis: fungsi aktif menjadi root, fungsi yang dapat dicapai melalui call ditempatkan pada lapisan kedalaman berikutnya.
- Fungsi yang tidak terjangkau tetap ditampilkan agar graph mencakup seluruh fungsi yang ditemukan.
- Scroll mengubah zoom; drag memindahkan canvas; klik node berpindah ke tampilan dekompilasi fungsi tersebut.

Canvas gelap, node beralamat, edge terarah, zoom, dan pan dipilih agar interaksi mendekati call graph pada Ghidra, IDA, atau Binary Ninja, sambil tetap dibuat dengan X11 native.

## Struktur kode

| File | Tanggung jawab |
| --- | --- |
| `main.cpp` | Entry point aplikasi. |
| `Models.hpp` | Model data `Function`, `Instruction`, dan node graph. |
| `ElfAnalyzer.hpp/.cpp` | Validasi ELF, ekstraksi simbol, disassembly, analisis, serta pseudocode. |
| `StudioWindow.hpp/.cpp` | GUI X11, navigasi fungsi, dan visualisasi call graph. |
| `test.cpp` | Test case `if` dan `for` loop. |

## Keterbatasan yang diketahui

- Hasil adalah pseudocode, bukan source C++ asli.
- Tipe data belum diinfer penuh; integer dipresentasikan sebagai `long`.
- Nama parameter tidak dapat dipulihkan tanpa debug information.
- Loop/branch yang dihilangkan compiler karena *dead-code elimination* tidak bisa direkonstruksi karena tidak ada lagi instruksinya di binary.
- Indirect call, exception, virtual dispatch, dan control-flow yang sangat kompleks belum didukung penuh.
