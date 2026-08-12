import argparse
import os
import subprocess
import sys

import pygame


REPO = os.path.dirname(os.path.abspath(__file__))
ARCHIVO_POR_DEFECTO = os.path.join(REPO, "resultados", "simulacion.eco")

BINARIO = os.path.join(
    REPO,
    "ecosistema_paralelo.exe" if os.name == "nt" else "ecosistema_paralelo",
)

# ------------------------------------------------------------------ colores
FONDO = (13, 13, 13)
SUPERFICIE = (26, 26, 25)
LINEA = (44, 44, 42)
TINTA = (255, 255, 255)
TINTA2 = (195, 194, 183)
MUTED = (137, 135, 129)
ACENTO = (57, 135, 229)

VACIO = (34, 34, 37)
PLANTA = (25, 158, 112)
HERBIVORO = (57, 135, 229)
CARNIVORO = (217, 89, 38)

COLOR_ESPECIE = {"P": PLANTA, "H": HERBIVORO, "C": CARNIVORO, ".": VACIO}

# ------------------------------------------------------------------ medidas
ANCHO, ALTO = 1180, 760
MARGEN = 18
CABECERA = 48
BARRA = 96
PANEL = 300


# =========================================================== carga de datos

class Corrida:
    """Una simulacion exportada, con sus cuadros y poblaciones."""

    def __init__(self, ruta):
        self.filas = 0
        self.columnas = 0
        self.semilla = 0
        self.modo = "secuencial"
        self.hilos = 1
        self.cuadros = []
        self.superficies = {}

        with open(ruta, "r", encoding="utf-8") as f:
            if f.readline().strip() != "ECO 1":
                raise ValueError("El archivo no tiene el formato esperado.")

            for linea in f:
                linea = linea.strip()
                if linea == "datos":
                    break
                if not linea:
                    continue
                clave, _, valor = linea.partition(" ")
                if clave == "filas":
                    self.filas = int(valor)
                elif clave == "columnas":
                    self.columnas = int(valor)
                elif clave == "semilla":
                    self.semilla = int(valor)
                elif clave == "modo":
                    self.modo = valor
                elif clave == "hilos":
                    self.hilos = int(valor)

            esperado = self.filas * self.columnas
            for linea in f:
                cuadro = linea.rstrip("\n")
                if len(cuadro) == esperado:
                    self.cuadros.append(cuadro)

        if not self.cuadros:
            raise ValueError("El archivo no contiene ningun cuadro.")

        # Los conteos se calculan una sola vez. str.count corre en C,
        # asi que esto es inmediato incluso con cientos de cuadros.
        self.conteos = [
            (c.count("P"), c.count("H"), c.count("C")) for c in self.cuadros
        ]
        self.tope = max(1, max(max(t) for t in self.conteos))
        self.extinto = sum(self.conteos[-1]) == 0

    def superficie(self, n):
        """Superficie del cuadro n, construida la primera vez que se pide."""
        if n not in self.superficies:
            cuadro = self.cuadros[n]
            crudo = b"".join(bytes(COLOR_ESPECIE[c]) for c in cuadro)
            self.superficies[n] = pygame.image.frombuffer(
                crudo, (self.columnas, self.filas), "RGB"
            )
        return self.superficies[n]


# =============================================================== generacion

def generar(args):
    """Corre el binario en C para producir el archivo de cuadros."""
    if not os.path.exists(BINARIO):
        sys.exit(
            "No se encontro el binario.\n"
            "Compila primero con 'make' y vuelve a intentar."
        )

    orden = [BINARIO, "--exportar", args.archivo, "--ticks", str(args.ticks)]

    if args.filas:
        orden += ["--filas", str(args.filas)]
    if args.modo:
        orden += ["--modo", args.modo]
    if args.hilos:
        orden += ["--hilos", str(args.hilos)]
    if args.prob_planta is not None:
        orden += ["--prob-alga", str(args.prob_planta)]
    if args.semilla is not None:
        orden += ["--semilla", str(args.semilla)]

    print("Generando la corrida...")
    r = subprocess.run(orden, cwd=REPO)
    if r.returncode != 0:
        sys.exit("El programa fallo al exportar la corrida.")


# ==================================================================== texto

def texto(sup, fuente, cadena, x, y, color=TINTA, derecha=False):
    img = fuente.render(cadena, True, color)
    r = img.get_rect()
    if derecha:
        r.topright = (x, y)
    else:
        r.topleft = (x, y)
    sup.blit(img, r)
    return r


def caja(sup, rect, relleno=SUPERFICIE, borde=LINEA, radio=9):
    pygame.draw.rect(sup, relleno, rect, border_radius=radio)
    if borde:
        pygame.draw.rect(sup, borde, rect, width=1, border_radius=radio)


# ===================================================================== main

def main():
    p = argparse.ArgumentParser(add_help=True)
    p.add_argument("--archivo", default=ARCHIVO_POR_DEFECTO)
    p.add_argument("--filas", type=int)
    p.add_argument("--ticks", type=int, default=300)
    p.add_argument("--modo", choices=["secuencial", "paralelo"])
    p.add_argument("--hilos", type=int)
    p.add_argument("--semilla", type=int)
    p.add_argument("--prob-planta", type=float, dest="prob_planta")
    p.add_argument(
        "--colapso",
        action="store_true",
        help="Escenario que termina con el ecosistema extinto",
    )
    args = p.parse_args()

    if args.colapso:
        args.filas = args.filas or 48
        args.prob_planta = 0.02
        args.ticks = max(args.ticks, 400)

    pide_generar = any(
        v is not None and v is not False
        for v in (args.filas, args.modo, args.hilos, args.semilla,
                  args.prob_planta, args.colapso or None)
    )

    if pide_generar or not os.path.exists(args.archivo):
        generar(args)

    corrida = Corrida(args.archivo)

    pygame.init()
    pygame.display.set_caption("Simulacion de ecosistema")
    pantalla = pygame.display.set_mode((ANCHO, ALTO))
    reloj = pygame.time.Clock()

    f_titulo = pygame.font.SysFont("segoeui,arial", 22, bold=True)
    f_normal = pygame.font.SysFont("segoeui,arial", 15)
    f_chico = pygame.font.SysFont("segoeui,arial", 13)
    f_dato = pygame.font.SysFont("segoeui,arial", 27, bold=True)

    # Geometria del area de la cuadricula
    area_w = ANCHO - PANEL - MARGEN * 3
    area_h = ALTO - CABECERA - BARRA - MARGEN
    lado = max(1, min(area_w // corrida.columnas, area_h // corrida.filas))
    grid_w = lado * corrida.columnas
    grid_h = lado * corrida.filas
    grid_x = MARGEN + (area_w - grid_w) // 2
    grid_y = CABECERA + (area_h - grid_h) // 2

    panel_x = ANCHO - PANEL - MARGEN
    total = len(corrida.cuadros) - 1

    barra_y = ALTO - BARRA
    r_play = pygame.Rect(MARGEN, barra_y + 12, 116, 36)
    r_reinicio = pygame.Rect(MARGEN + 126, barra_y + 12, 106, 36)
    vel_x, vel_w = MARGEN + 300, 180
    r_vel = pygame.Rect(vel_x, barra_y + 22, vel_w, 16)
    r_scrub = pygame.Rect(MARGEN, barra_y + 66, ANCHO - MARGEN * 2, 16)

    i = 0
    corriendo = False
    velocidad = 14.0
    acumulado = 0.0
    arrastrando = None

    def posicion_a_tick(mx):
        t = (mx - r_scrub.x) / max(1, r_scrub.w)
        return max(0, min(total, round(t * total)))

    def posicion_a_velocidad(mx):
        t = (mx - r_vel.x) / max(1, r_vel.w)
        return max(1.0, min(60.0, 1.0 + t * 59.0))

    while True:
        dt = reloj.tick(60) / 1000.0

        for e in pygame.event.get():
            if e.type == pygame.QUIT:
                pygame.quit()
                return
            if e.type == pygame.KEYDOWN:
                if e.key == pygame.K_ESCAPE:
                    pygame.quit()
                    return
                if e.key == pygame.K_SPACE:
                    if i >= total:
                        i = 0
                    corriendo = not corriendo
                    acumulado = 0.0
                if e.key == pygame.K_r:
                    i, corriendo, acumulado = 0, False, 0.0
                if e.key == pygame.K_RIGHT:
                    corriendo = False
                    i = min(total, i + 1)
                if e.key == pygame.K_LEFT:
                    corriendo = False
                    i = max(0, i - 1)
            if e.type == pygame.MOUSEBUTTONDOWN and e.button == 1:
                if r_play.collidepoint(e.pos):
                    if i >= total:
                        i = 0
                    corriendo = not corriendo
                    acumulado = 0.0
                elif r_reinicio.collidepoint(e.pos):
                    i, corriendo, acumulado = 0, False, 0.0
                elif r_scrub.inflate(0, 20).collidepoint(e.pos):
                    arrastrando = "tick"
                    corriendo = False
                    i = posicion_a_tick(e.pos[0])
                elif r_vel.inflate(0, 20).collidepoint(e.pos):
                    arrastrando = "vel"
                    velocidad = posicion_a_velocidad(e.pos[0])
            if e.type == pygame.MOUSEBUTTONUP:
                arrastrando = None
            if e.type == pygame.MOUSEMOTION and arrastrando:
                if arrastrando == "tick":
                    i = posicion_a_tick(e.pos[0])
                else:
                    velocidad = posicion_a_velocidad(e.pos[0])

        if corriendo:
            acumulado += dt * velocidad
            while acumulado >= 1.0:
                acumulado -= 1.0
                if i < total:
                    i += 1
                else:
                    corriendo = False
                    acumulado = 0.0
                    break

        # ------------------------------------------------------------ pintar
        pantalla.fill(FONDO)

        texto(pantalla, f_titulo, "Simulacion de ecosistema", MARGEN, 12)
        texto(
            pantalla,
            f_chico,
            "{}x{}  ({})".format(
                corrida.filas,
                corrida.columnas,
                corrida.modo
                + ("" if corrida.modo == "secuencial"
                   else ", {} hilos".format(corrida.hilos or "auto")),
            ),
            MARGEN + 268,
            20,
            MUTED,
        )

        # Cuadricula
        marco = pygame.Rect(grid_x - 6, grid_y - 6, grid_w + 12, grid_h + 12)
        caja(pantalla, marco)
        sup = corrida.superficie(i)
        pantalla.blit(pygame.transform.scale(sup, (grid_w, grid_h)),
                      (grid_x, grid_y))

        # Panel derecho
        pc, ph, pcar = corrida.conteos[i]
        y = CABECERA
        for etiqueta, valor, color in (
            ("Plantas", pc, PLANTA),
            ("Herbivoros", ph, HERBIVORO),
            ("Carnivoros", pcar, CARNIVORO),
        ):
            r = pygame.Rect(panel_x, y, PANEL, 64)
            caja(pantalla, r)
            pygame.draw.rect(
                pantalla, color, pygame.Rect(panel_x + 14, y + 26, 11, 11),
                border_radius=3,
            )
            texto(pantalla, f_normal, etiqueta, panel_x + 33, y + 20, TINTA2)
            texto(pantalla, f_dato, "{:,}".format(valor).replace(",", " "),
                  panel_x + PANEL - 14, y + 16, TINTA, derecha=True)
            y += 72

        # Grafica de poblaciones
        gr = pygame.Rect(panel_x, y, PANEL, 168)
        caja(pantalla, gr)
        texto(pantalla, f_chico, "Poblaciones", panel_x + 14, y + 10, TINTA2)
        gx, gy = gr.x + 14, gr.y + 34
        gw, gh = gr.w - 28, gr.h - 50
        pygame.draw.line(pantalla, LINEA, (gx, gy + gh), (gx + gw, gy + gh))
        if total > 0:
            for idx, color in ((0, PLANTA), (1, HERBIVORO), (2, CARNIVORO)):
                puntos = [
                    (gx + k * gw / total,
                     gy + gh - (c[idx] / corrida.tope) * gh)
                    for k, c in enumerate(corrida.conteos)
                ]
                if len(puntos) > 1:
                    pygame.draw.lines(pantalla, color, False, puntos, 2)
            cur = gx + i * gw / total
            pygame.draw.line(pantalla, MUTED, (cur, gy - 4), (cur, gy + gh))

        # Bloque de informacion
        y += 180
        info = pygame.Rect(panel_x, y, PANEL, 116)
        caja(pantalla, info)
        datos = [
            ("Semilla", str(corrida.semilla)),
            ("Ticks simulados", str(total)),
            ("Modo", corrida.modo),
        ]
        yy = y + 14
        for etiqueta, valor in datos:
            texto(pantalla, f_chico, etiqueta, panel_x + 14, yy, MUTED)
            texto(pantalla, f_chico, valor, panel_x + PANEL - 14, yy,
                  TINTA2, derecha=True)
            yy += 22

        texto(pantalla, f_chico, "Estado", panel_x + 14, yy, MUTED)
        if i >= total:
            estado_txt, color = (
                ("extinto", CARNIVORO) if corrida.extinto
                else ("fin de los ticks", TINTA2)
            )
        else:
            estado_txt, color = ("en curso", PLANTA)
        texto(pantalla, f_chico, estado_txt, panel_x + PANEL - 14, yy,
              color, derecha=True)

        # Barra inferior
        caja(pantalla, pygame.Rect(0, barra_y, ANCHO, BARRA), FONDO, None, 0)
        pygame.draw.line(pantalla, LINEA, (0, barra_y), (ANCHO, barra_y))

        caja(pantalla, r_play, ACENTO, ACENTO, 8)
        etiqueta = "Pausar" if corriendo else "Reproducir"
        img = f_normal.render(etiqueta, True, (255, 255, 255))
        pantalla.blit(img, img.get_rect(center=r_play.center))

        caja(pantalla, r_reinicio, SUPERFICIE, LINEA, 8)
        img = f_normal.render("Reiniciar", True, TINTA)
        pantalla.blit(img, img.get_rect(center=r_reinicio.center))

        texto(pantalla, f_chico, "Velocidad", vel_x, barra_y + 2, MUTED)
        pygame.draw.rect(pantalla, LINEA, r_vel, border_radius=8)
        frac = (velocidad - 1.0) / 59.0
        pygame.draw.rect(
            pantalla, ACENTO,
            pygame.Rect(r_vel.x, r_vel.y, int(r_vel.w * frac), r_vel.h),
            border_radius=8,
        )
        pygame.draw.circle(
            pantalla, (255, 255, 255),
            (int(r_vel.x + r_vel.w * frac), r_vel.centery), 8
        )
        texto(pantalla, f_chico, "{:.0f} ticks/s".format(velocidad),
              r_vel.right + 14, barra_y + 24, TINTA2)

        texto(pantalla, f_normal, "Tick {} / {}".format(i, total),
              ANCHO - MARGEN, barra_y + 20, TINTA, derecha=True)

        pygame.draw.rect(pantalla, LINEA, r_scrub, border_radius=8)
        frac = i / total if total else 0
        pygame.draw.rect(
            pantalla, ACENTO,
            pygame.Rect(r_scrub.x, r_scrub.y, int(r_scrub.w * frac),
                        r_scrub.h),
            border_radius=8,
        )
        pygame.draw.circle(
            pantalla, (255, 255, 255),
            (int(r_scrub.x + r_scrub.w * frac), r_scrub.centery), 8
        )

        pygame.display.flip()


if __name__ == "__main__":
    main()
