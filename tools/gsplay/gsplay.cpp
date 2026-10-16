// Reproduz um GS dump do PCSX2 (.gs ou .gs.zst) no GS do runtime e compara com o frame do PCSX2.
// Separa bug de GS de bug de VU1/EE: o dump já é a saída do VU1, então se aqui sair errado o culpado é o GS.
//
// Uso: gsplay <dump.gs[.zst]> <pasta_saida>
// Gera em pasta_saida: pcsx2.png (screenshot embutido no dump), frameN.png (runtime a cada VSync),
// compareN.png (lado a lado) e imprime o erro médio por canal de cada frame contra o PCSX2.
//
// Formato (pcsx2/GS/GSDump.h, v2.9): [0xFFFFFFFF][header_size][GSDumpHeader][serial][screenshot]
// [estado GSState::Freeze][priv regs 0x2000] e pacotes: 0=transfer(path,size,data) 1=vsync(field)
// 2=readfifo(size) 3=priv regs(0x2000).

#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_types.h"
#include "raylib.h"
#include <zstd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kVramSize = 4u * 1024u * 1024u;
    constexpr uint32_t kPrivSize = 0x2000u;
    constexpr uint32_t kStateTail = 4u * (16u + 4u) + 4u; // 4 GIFPath (tag + reg) + m_q

    struct DumpHeader
    {
        uint32_t stateVersion, stateSize, serialOffset, serialSize, crc;
        uint32_t shotWidth, shotHeight, shotOffset, shotSize;
    };

    std::vector<uint8_t> readFile(const char *path)
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (raw.size() < 4 || raw[0] != 0x28 || raw[1] != 0xB5 || raw[2] != 0x2F || raw[3] != 0xFD) // magic zstd
            return raw;

        std::vector<uint8_t> out;
        ZSTD_DCtx *ctx = ZSTD_createDCtx();
        std::vector<uint8_t> buf(ZSTD_DStreamOutSize());
        ZSTD_inBuffer inBuf{raw.data(), raw.size(), 0};
        while (inBuf.pos < inBuf.size)
        {
            ZSTD_outBuffer outBuf{buf.data(), buf.size(), 0};
            const size_t r = ZSTD_decompressStream(ctx, &outBuf, &inBuf);
            if (ZSTD_isError(r))
            {
                std::fprintf(stderr, "zstd: %s\n", ZSTD_getErrorName(r));
                break;
            }
            out.insert(out.end(), buf.data(), buf.data() + outBuf.pos);
        }
        ZSTD_freeDCtx(ctx);
        return out;
    }

    uint64_t u64(const uint8_t *p)
    {
        uint64_t v;
        std::memcpy(&v, p, 8);
        return v;
    }

    // Registradores privilegiados no layout do PCSX2 (GSPrivRegSet): blocos de 16 bytes, CSR em 0x1000.
    void applyPrivRegs(GSRegisters &regs, const uint8_t *p)
    {
        uint64_t *dst[] = {&regs.pmode, &regs.smode1, &regs.smode2, &regs.srfsh, &regs.synch1, &regs.synch2,
                           &regs.syncv, &regs.dispfb1, &regs.display1, &regs.dispfb2, &regs.display2,
                           &regs.extbuf, &regs.extdata, &regs.extwrite, &regs.bgcolor};
        for (size_t i = 0; i < std::size(dst); ++i)
            *dst[i] = u64(p + i * 0x10);
        regs.imr = u64(p + 0x1010);
        regs.busdir = u64(p + 0x1040);
        regs.siglblid = u64(p + 0x1080);
    }

    // Registradores de desenho do estado salvo (ordem de GSState::Freeze). TRXDIR e XYZ ficam de fora:
    // escrever TRXDIR inicia uma transferência e XYZ dispara vértice.
    void applyDrawState(GS &gs, const uint8_t *s)
    {
        const uint8_t env[] = {GS_REG_PRIM, GS_REG_PRMODECONT, GS_REG_TEXCLUT, GS_REG_SCANMSK, GS_REG_TEXA,
                               GS_REG_FOGCOL, GS_REG_DIMX, GS_REG_DTHE, GS_REG_COLCLAMP, GS_REG_PABE,
                               GS_REG_BITBLTBUF, 0xFF /*TRXDIR*/, GS_REG_TRXPOS, GS_REG_TRXREG};
        const uint8_t *p = s + 4;
        for (uint8_t reg : env)
        {
            if (reg != 0xFF)
                gs.writeRegister(reg, u64(p));
            p += 8;
        }
        p += 8; // TRXREG obsoleto

        for (int c = 0; c < 2; ++c)
        {
            const uint8_t ctx[] = {GS_REG_XYOFFSET_1, GS_REG_TEX0_1, GS_REG_TEX1_1, GS_REG_CLAMP_1,
                                   GS_REG_MIPTBP1_1, GS_REG_MIPTBP2_1, GS_REG_SCISSOR_1, GS_REG_ALPHA_1,
                                   GS_REG_TEST_1, GS_REG_FBA_1, GS_REG_FRAME_1, GS_REG_ZBUF_1};
            for (uint8_t reg : ctx)
            {
                gs.writeRegister(static_cast<uint8_t>(reg + c), u64(p));
                p += 8;
            }
        }
        gs.writeRegister(GS_REG_RGBAQ, u64(p));
        gs.writeRegister(GS_REG_ST, u64(p + 8));
        gs.writeRegister(GS_REG_UV, u64(p + 16));
        gs.writeRegister(GS_REG_FOG, u64(p + 24));
    }

    Image toImage(const std::vector<uint8_t> &rgba, uint32_t w, uint32_t h)
    {
        Image img = GenImageColor(static_cast<int>(w), static_cast<int>(h), BLACK);
        std::memcpy(img.data, rgba.data(), static_cast<size_t>(w) * h * 4u);
        return img;
    }

    // Erro médio absoluto por canal (0..255) do frame do runtime redimensionado para o tamanho da referência.
    double compare(const Image &ref, const Image &run, Image &sideBySide)
    {
        Image scaled = ImageCopy(run);
        ImageResizeNN(&scaled, ref.width, ref.height);
        const auto *a = static_cast<const uint8_t *>(ref.data);
        const auto *b = static_cast<const uint8_t *>(scaled.data);
        double sum = 0;
        const size_t n = static_cast<size_t>(ref.width) * ref.height;
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c)
                sum += std::abs(int(a[i * 4 + c]) - int(b[i * 4 + c]));

        sideBySide = GenImageColor(ref.width * 2, ref.height, BLACK);
        ImageDraw(&sideBySide, ref, {0, 0, float(ref.width), float(ref.height)},
                  {0, 0, float(ref.width), float(ref.height)}, WHITE);
        ImageDraw(&sideBySide, scaled, {0, 0, float(ref.width), float(ref.height)},
                  {float(ref.width), 0, float(ref.width), float(ref.height)}, WHITE);
        UnloadImage(scaled);
        return sum / (n * 3.0);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "uso: %s <dump.gs[.zst]> <pasta_saida>\n", argv[0]);
        return 1;
    }
    SetTraceLogLevel(LOG_WARNING);
    const std::string outDir = argv[2];
    const std::vector<uint8_t> d = readFile(argv[1]);

    uint32_t magic = 0, headerSize = 0;
    std::memcpy(&magic, d.data(), 4);
    std::memcpy(&headerSize, d.data() + 4, 4);
    if (magic != 0xFFFFFFFFu)
    {
        std::fprintf(stderr, "dump em formato antigo, não suportado\n");
        return 1;
    }
    DumpHeader h;
    std::memcpy(&h, d.data() + 8, sizeof(h));
    const uint8_t *hdr = d.data() + 8;
    std::printf("serial=%.*s state_version=%u state_size=%u shot=%ux%u\n", int(h.serialSize),
                reinterpret_cast<const char *>(hdr + h.serialOffset), h.stateVersion, h.stateSize, h.shotWidth,
                h.shotHeight);

    Image ref{};
    if (h.shotSize != 0)
    {
        std::vector<uint8_t> px(hdr + h.shotOffset, hdr + h.shotOffset + h.shotSize);
        for (size_t i = 3; i < px.size(); i += 4)
            px[i] = 0xFF;
        ref = toImage(px, h.shotWidth, h.shotHeight);
        ExportImage(ref, (outDir + "/pcsx2.png").c_str());
    }

    size_t pos = 8 + headerSize;
    const uint8_t *state = d.data() + pos;
    pos += h.stateSize;

    static std::vector<uint8_t> vram(kVramSize);
    std::memcpy(vram.data(), state + h.stateSize - kStateTail - kVramSize, kVramSize);
    static GSRegisters priv{};
    applyPrivRegs(priv, d.data() + pos);
    pos += kPrivSize;

    GS gs;
    gs.init(vram.data(), kVramSize, &priv);
    applyDrawState(gs, state);

    int frame = 0;
    uint64_t transfers = 0, bytes = 0;
    while (pos < d.size())
    {
        const uint8_t type = d[pos++];
        if (type == 0)
        {
            uint32_t size;
            std::memcpy(&size, &d[pos + 1], 4);
            gs.processGIFPacket(&d[pos + 5], size);
            pos += 5 + size;
            ++transfers;
            bytes += size;
        }
        else if (type == 1)
        {
            ++pos;
            gs.latchHostPresentationFrame();
            std::vector<uint8_t> px;
            uint32_t w = 0, hgt = 0, displayFbp = 0, sourceFbp = 0;
            bool preferred = false;
            if (!gs.copyLatchedHostPresentationFrame(px, w, hgt, &displayFbp, &sourceFbp, &preferred))
            {
                std::printf("vsync %d: sem frame apresentável\n", frame++);
                continue;
            }
            for (size_t i = 3; i < px.size(); i += 4)
                px[i] = 0xFF;
            Image img = toImage(px, w, hgt);
            const std::string name = outDir + "/frame" + std::to_string(frame);
            ExportImage(img, (name + ".png").c_str());
            if (ref.data)
            {
                Image side{};
                const double err = compare(ref, img, side);
                ExportImage(side, (outDir + "/compare" + std::to_string(frame) + ".png").c_str());
                UnloadImage(side);
                std::printf("vsync %d: %ux%u fbp=0x%x src=0x%x erro_medio=%.1f/255 (%u transfers, %.1f MB)\n", frame, w,
                            hgt, displayFbp, sourceFbp, err, unsigned(transfers), bytes / 1048576.0);
            }
            else
                std::printf("vsync %d: %ux%u fbp=0x%x src=0x%x (%u transfers, %.1f MB)\n", frame, w, hgt, displayFbp,
                            sourceFbp, unsigned(transfers), bytes / 1048576.0);
            UnloadImage(img);
            ++frame;
        }
        else if (type == 2)
            pos += 4;
        else if (type == 3)
        {
            applyPrivRegs(priv, &d[pos]);
            pos += kPrivSize;
        }
        else
        {
            std::fprintf(stderr, "pacote desconhecido %u em %zu\n", type, pos - 1);
            return 1;
        }
    }
    return 0;
}
