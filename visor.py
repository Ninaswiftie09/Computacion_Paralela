"""Visor grafico de la simulacion de ecosistema.

Interfaz completa sobre pygame. Permite configurar la corrida,
ejecutarla, verla cuadro por cuadro, medir el rendimiento y
buscar race conditions, sin tocar la linea de comandos.

Uso:
    python visor.py

Controles del visor:
    Espacio      reproducir o pausar
    Flechas      un tick atras o adelante
    R            reiniciar
    Esc          volver a configuracion
"""

import csv
import os
import queue
import random
import subprocess
import sys
import threading

import pygame

from visor_ui import (
    ACENTO, APAGADO, CARNIVORO, COLOR_ESPECIE, ELEVADA, FONDO, HERBIVORO,
    LINEA, MUTED, PLANTA, SUPERFICIE, TINTA, TINTA2,
    Boton, Deslizador, Segmentado, caja, texto,
)


REPO = os.path.dirname(os.path.abspath(__file__))
RESULTADOS = os.path.join(REPO, "resultados")
ARCHIVO_CORRIDA = os.path.join(RESULTADOS, "simulacion.eco")
ARCHIVO_BENCH = os.path.join(RESULTADOS, "benchmark_interfaz.csv")

BINARIO = os.path.join(
    REPO,
    "ecosistema_paralelo.exe" if os.name == "nt" else "ecosistema_paralelo",
)

ANCHO, ALTO = 1180, 780
MARGEN = 20
CABECERA = 56


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

        # str.count corre en C, asi que contar todos los cuadros de
        # una vez es inmediato incluso con cientos de ellos.
        self.conteos = [
            (c.count("P"), c.count("H"), c.count("C")) for c in self.cuadros
        ]
        self.tope = max(1, max(max(t) for t in self.conteos))
        self.extinto = sum(self.conteos[-1]) == 0

    def superficie(self, n):
        """Superficie del cuadro n, construida al pedirla por primera vez."""
        if n not in self.superficies:
            crudo = b"".join(bytes(COLOR_ESPECIE[c]) for c in self.cuadros[n])
            self.superficies[n] = pygame.image.frombuffer(
                crudo, (self.columnas, self.filas), "RGB")
        return self.superficies[n]


# =================================================== ejecucion en segundo plano

class Tarea:
    """Un proceso del binario corriendo sin bloquear la interfaz."""

    def __init__(self, nombre, orden):
        self.nombre = nombre
        self.lineas = queue.Queue()
        self.terminada = False
        self.codigo = None
        self.hilo = threading.Thread(
            target=self._correr, args=(orden,), daemon=True)
        self.hilo.start()

    def _correr(self, orden):
        try:
            p = subprocess.Popen(
                orden, cwd=REPO, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                errors="replace", bufsize=1,
            )
            for linea in p.stdout:
                self.lineas.put(linea.rstrip("\n"))
            p.wait()
            self.codigo = p.returncode
        except OSError as e:
            self.lineas.put("Error al ejecutar: {}".format(e))
            self.codigo = 1
        self.terminada = True


# ====================================================================== app

class App:

    def __init__(self):
        pygame.init()
        pygame.display.set_caption("Simulacion de ecosistema")
        self.pantalla = pygame.display.set_mode((ANCHO, ALTO))
        self.reloj = pygame.time.Clock()

        self.f_titulo = pygame.font.SysFont("segoeui,arial", 21, bold=True)
        self.f_normal = pygame.font.SysFont("segoeui,arial", 15)
        self.f_chico = pygame.font.SysFont("segoeui,arial", 13)
        self.f_mono = pygame.font.SysFont("consolas,couriernew", 13)
        self.f_dato = pygame.font.SysFont("segoeui,arial", 26, bold=True)

        self.vista = "config"
        self.corrida = None
        self.tarea = None
        self.consola = []
        self.mensaje = ""
        self.bench = None

        self._construir_navegacion()
        self._construir_config()
        self._construir_herramientas()

        # Estado del visor
        self.i = 0
        self.reproduciendo = False
        self.velocidad = 14.0
        self.acumulado = 0.0
        self.arrastrando = None

    # ------------------------------------------------------------ navegacion

    def _construir_navegacion(self):
        self.nav = []
        etiquetas = [
            ("config", "Configuracion"),
            ("visor", "Visor"),
            ("herramientas", "Herramientas"),
        ]
        x = ANCHO - MARGEN - 3 * 132
        for clave, etiqueta in etiquetas:
            self.nav.append((clave, Boton((x, 12, 128, 32), etiqueta)))
            x += 132

    def dibujar_navegacion(self):
        texto(self.pantalla, self.f_titulo, "Simulacion de ecosistema",
              MARGEN, 16)
        for clave, boton in self.nav:
            activo = (clave == self.vista)
            caja(self.pantalla, boton.rect,
                 ACENTO if activo else (ELEVADA if boton.encima else FONDO),
                 ACENTO if activo else LINEA, 8)
            color = (255, 255, 255) if activo else TINTA2
            img = self.f_normal.render(boton.etiqueta, True, color)
            self.pantalla.blit(img, img.get_rect(center=boton.rect.center))
        pygame.draw.line(self.pantalla, LINEA, (0, CABECERA), (ANCHO, CABECERA))

    # ---------------------------------------------------------- configuracion

    def _construir_config(self):
        col1 = MARGEN
        col2 = ANCHO // 2 + 10
        ancho = ANCHO // 2 - MARGEN - 30
        y = CABECERA + 40
        salto = 74

        self.d_lado = Deslizador((col1, y, ancho, 40), "Lado de la cuadricula",
                                 16, 256, 64, sufijo=" celdas")
        self.d_ticks = Deslizador((col1, y + salto, ancho, 40), "Ticks",
                                  20, 600, 300, paso=10)
        self.d_semilla = Deslizador((col1, y + salto * 2, ancho, 40), "Semilla",
                                    1, 99999, 20260809 % 99999)
        self.d_prob = Deslizador((col1, y + salto * 3, ancho, 40),
                                 "Probabilidad de reproduccion de plantas",
                                 0.0, 1.0, 0.30, entero=False, paso=0.01)

        self.s_modo = Segmentado((col2, y, ancho, 58), "Modo de ejecucion",
                                 ["Secuencial", "Paralelo"], 1)
        self.d_hilos = Deslizador((col2, y + salto, ancho, 40), "Hilos",
                                  1, 32, 8)
        self.d_plantas = Deslizador((col2, y + salto * 2, ancho, 40),
                                    "Plantas", 0, 60, 27, sufijo="%")
        self.d_herb = Deslizador((col2, y + salto * 3, ancho, 40),
                                 "Herbivoros", 0, 30, 7, sufijo="%")
        self.d_carn = Deslizador((col2, y + salto * 4, ancho, 40),
                                 "Carnivoros", 0, 20, 2, sufijo="%")

        self.deslizadores = [
            self.d_lado, self.d_ticks, self.d_semilla, self.d_prob,
            self.d_hilos, self.d_plantas, self.d_herb, self.d_carn,
        ]

        yb = ALTO - 76
        self.b_simular = Boton((MARGEN, yb, 220, 44), "Ejecutar simulacion",
                               primario=True)
        self.b_colapso = Boton((MARGEN + 232, yb, 200, 44),
                               "Escenario de colapso")
        self.b_azar = Boton((MARGEN + 444, yb, 150, 44), "Semilla al azar")

    def _capacidad(self):
        return self.d_lado.valor ** 2

    def _poblaciones(self):
        cap = self._capacidad()
        return (
            int(cap * self.d_plantas.valor / 100),
            int(cap * self.d_herb.valor / 100),
            int(cap * self.d_carn.valor / 100),
        )

    def dibujar_config(self):
        cap = self._capacidad()
        plantas, herb, carn = self._poblaciones()

        # Las etiquetas de poblacion muestran tambien el numero absoluto,
        # que es lo que realmente recibe la simulacion.
        self.d_plantas.sufijo = "%  ({})".format(plantas)
        self.d_herb.sufijo = "%  ({})".format(herb)
        self.d_carn.sufijo = "%  ({})".format(carn)
        self.d_lado.sufijo = "  ({} celdas)".format(cap)

        self.d_hilos.activo = (self.s_modo.indice == 1)

        for d in self.deslizadores:
            d.dibujar(self.pantalla, self.f_chico, self.f_normal)
        self.s_modo.dibujar(self.pantalla, self.f_chico, self.f_normal)

        total = plantas + herb + carn
        y = ALTO - 132
        if total > cap:
            texto(self.pantalla, self.f_normal,
                  "Las poblaciones suman {} y solo hay {} celdas.".format(
                      total, cap), MARGEN, y, CARNIVORO)
        elif self.mensaje:
            texto(self.pantalla, self.f_normal, self.mensaje, MARGEN, y, TINTA2)
        else:
            texto(self.pantalla, self.f_chico,
                  "Ocupacion inicial del {:.0f}%.".format(100 * total / cap),
                  MARGEN, y, MUTED)

        ocupado = (self.tarea is not None and not self.tarea.terminada)
        self.b_simular.activo = (total <= cap) and not ocupado
        self.b_colapso.activo = not ocupado
        self.b_azar.activo = not ocupado
        self.b_simular.etiqueta = (
            "Generando..." if ocupado else "Ejecutar simulacion")

        for b in (self.b_simular, self.b_colapso, self.b_azar):
            b.dibujar(self.pantalla, self.f_normal)

    def eventos_config(self, e):
        for d in self.deslizadores:
            d.evento(e)
        self.s_modo.evento(e)

        if self.b_azar.evento(e):
            self.d_semilla.valor = random.randint(1, 99999)
        if self.b_simular.evento(e):
            self.lanzar_simulacion()
        if self.b_colapso.evento(e):
            self.d_prob.valor = 0.02
            self.d_lado.valor = min(self.d_lado.valor, 64)
            self.lanzar_simulacion()

    def lanzar_simulacion(self):
        plantas, herb, carn = self._poblaciones()
        orden = [
            BINARIO,
            "--exportar", ARCHIVO_CORRIDA,
            "--filas", str(self.d_lado.valor),
            "--ticks", str(self.d_ticks.valor),
            "--semilla", str(self.d_semilla.valor),
            "--prob-alga", str(self.d_prob.valor),
            "--plantas", str(plantas),
            "--herbivoros", str(herb),
            "--carnivoros", str(carn),
            "--modo", "paralelo" if self.s_modo.indice == 1 else "secuencial",
        ]
        if self.s_modo.indice == 1:
            orden += ["--hilos", str(self.d_hilos.valor)]

        self.mensaje = ""
        self.consola = []
        self.tarea = Tarea("simulacion", orden)

    # ------------------------------------------------------------- visor

    def dibujar_visor(self):
        if self.corrida is None:
            texto(self.pantalla, self.f_normal,
                  "Todavia no hay ninguna corrida. Ve a Configuracion y "
                  "ejecuta una simulacion.", MARGEN, CABECERA + 40, TINTA2)
            return

        c = self.corrida
        panel = 300
        barra_alto = 92
        barra_y = ALTO - barra_alto

        area_w = ANCHO - panel - MARGEN * 3
        area_h = barra_y - CABECERA - MARGEN * 2
        lado = max(1, min(area_w // c.columnas, area_h // c.filas))
        gw, gh = lado * c.columnas, lado * c.filas
        gx = MARGEN + (area_w - gw) // 2
        gy = CABECERA + MARGEN + (area_h - gh) // 2

        caja(self.pantalla, pygame.Rect(gx - 6, gy - 6, gw + 12, gh + 12))
        self.pantalla.blit(
            pygame.transform.scale(c.superficie(self.i), (gw, gh)), (gx, gy))

        panel_x = ANCHO - panel - MARGEN
        total = len(c.cuadros) - 1
        pc, ph, pcar = c.conteos[self.i]

        y = CABECERA + MARGEN
        for etiqueta, valor, color in (
            ("Plantas", pc, PLANTA),
            ("Herbivoros", ph, HERBIVORO),
            ("Carnivoros", pcar, CARNIVORO),
        ):
            r = pygame.Rect(panel_x, y, panel, 62)
            caja(self.pantalla, r)
            pygame.draw.rect(self.pantalla, color,
                             pygame.Rect(panel_x + 14, y + 25, 11, 11),
                             border_radius=3)
            texto(self.pantalla, self.f_normal, etiqueta, panel_x + 33, y + 19,
                  TINTA2)
            texto(self.pantalla, self.f_dato, "{}".format(valor),
                  panel_x + panel - 14, y + 15, TINTA, derecha=True)
            y += 70

        gr = pygame.Rect(panel_x, y, panel, 160)
        caja(self.pantalla, gr)
        texto(self.pantalla, self.f_chico, "Poblaciones", gr.x + 14, gr.y + 10,
              TINTA2)
        px, py = gr.x + 14, gr.y + 32
        pw, phh = gr.w - 28, gr.h - 48
        pygame.draw.line(self.pantalla, LINEA, (px, py + phh),
                         (px + pw, py + phh))
        if total > 0:
            for idx, color in ((0, PLANTA), (1, HERBIVORO), (2, CARNIVORO)):
                puntos = [
                    (px + k * pw / total, py + phh - (t[idx] / c.tope) * phh)
                    for k, t in enumerate(c.conteos)
                ]
                if len(puntos) > 1:
                    pygame.draw.lines(self.pantalla, color, False, puntos, 2)
            cur = px + self.i * pw / total
            pygame.draw.line(self.pantalla, MUTED, (cur, py - 4),
                             (cur, py + phh))

        y += 172
        info = pygame.Rect(panel_x, y, panel, 112)
        caja(self.pantalla, info)
        filas_info = [
            ("Cuadricula", "{}x{}".format(c.filas, c.columnas)),
            ("Modo", c.modo + ("" if c.modo == "secuencial"
                               else " x{}".format(c.hilos or "auto"))),
            ("Semilla", str(c.semilla)),
        ]
        yy = y + 13
        for etiqueta, valor in filas_info:
            texto(self.pantalla, self.f_chico, etiqueta, info.x + 14, yy, MUTED)
            texto(self.pantalla, self.f_chico, valor, info.right - 14, yy,
                  TINTA2, derecha=True)
            yy += 21

        texto(self.pantalla, self.f_chico, "Estado", info.x + 14, yy, MUTED)
        if self.i >= total:
            est, col = (("extinto", CARNIVORO) if c.extinto
                        else ("fin de los ticks", TINTA2))
        else:
            est, col = ("en curso", PLANTA)
        texto(self.pantalla, self.f_chico, est, info.right - 14, yy, col,
              derecha=True)

        # Barra de control
        pygame.draw.line(self.pantalla, LINEA, (0, barra_y), (ANCHO, barra_y))
        self.r_play = pygame.Rect(MARGEN, barra_y + 12, 116, 36)
        self.r_reinicio = pygame.Rect(MARGEN + 126, barra_y + 12, 106, 36)
        self.r_vel = pygame.Rect(MARGEN + 300, barra_y + 22, 180, 14)
        self.r_scrub = pygame.Rect(MARGEN, barra_y + 64, ANCHO - MARGEN * 2, 14)

        caja(self.pantalla, self.r_play, ACENTO, ACENTO, 8)
        img = self.f_normal.render(
            "Pausar" if self.reproduciendo else "Reproducir", True,
            (255, 255, 255))
        self.pantalla.blit(img, img.get_rect(center=self.r_play.center))

        caja(self.pantalla, self.r_reinicio, SUPERFICIE, LINEA, 8)
        img = self.f_normal.render("Reiniciar", True, TINTA)
        self.pantalla.blit(img, img.get_rect(center=self.r_reinicio.center))

        texto(self.pantalla, self.f_chico, "Velocidad", self.r_vel.x,
              barra_y + 4, MUTED)
        self._barra(self.r_vel, (self.velocidad - 1) / 59.0)
        texto(self.pantalla, self.f_chico,
              "{:.0f} ticks/s".format(self.velocidad), self.r_vel.right + 14,
              barra_y + 22, TINTA2)

        texto(self.pantalla, self.f_normal,
              "Tick {} / {}".format(self.i, total), ANCHO - MARGEN,
              barra_y + 20, TINTA, derecha=True)
        self._barra(self.r_scrub, self.i / total if total else 0)

    def _barra(self, rect, frac):
        pygame.draw.rect(self.pantalla, LINEA, rect, border_radius=7)
        if frac > 0:
            pygame.draw.rect(
                self.pantalla, ACENTO,
                pygame.Rect(rect.x, rect.y, int(rect.w * frac), rect.h),
                border_radius=7)
        pygame.draw.circle(self.pantalla, (255, 255, 255),
                           (int(rect.x + rect.w * frac), rect.centery), 8)

    def eventos_visor(self, e):
        if self.corrida is None:
            return
        total = len(self.corrida.cuadros) - 1

        def a_tick(mx):
            t = (mx - self.r_scrub.x) / max(1, self.r_scrub.w)
            return max(0, min(total, round(t * total)))

        def a_vel(mx):
            t = (mx - self.r_vel.x) / max(1, self.r_vel.w)
            return max(1.0, min(60.0, 1.0 + t * 59.0))

        if e.type == pygame.KEYDOWN:
            if e.key == pygame.K_SPACE:
                if self.i >= total:
                    self.i = 0
                self.reproduciendo = not self.reproduciendo
                self.acumulado = 0.0
            elif e.key == pygame.K_r:
                self.i, self.reproduciendo, self.acumulado = 0, False, 0.0
            elif e.key == pygame.K_RIGHT:
                self.reproduciendo = False
                self.i = min(total, self.i + 1)
            elif e.key == pygame.K_LEFT:
                self.reproduciendo = False
                self.i = max(0, self.i - 1)

        if e.type == pygame.MOUSEBUTTONDOWN and e.button == 1:
            if self.r_play.collidepoint(e.pos):
                if self.i >= total:
                    self.i = 0
                self.reproduciendo = not self.reproduciendo
                self.acumulado = 0.0
            elif self.r_reinicio.collidepoint(e.pos):
                self.i, self.reproduciendo, self.acumulado = 0, False, 0.0
            elif self.r_scrub.inflate(0, 20).collidepoint(e.pos):
                self.arrastrando = "tick"
                self.reproduciendo = False
                self.i = a_tick(e.pos[0])
            elif self.r_vel.inflate(0, 20).collidepoint(e.pos):
                self.arrastrando = "vel"
                self.velocidad = a_vel(e.pos[0])
        if e.type == pygame.MOUSEBUTTONUP:
            self.arrastrando = None
        if e.type == pygame.MOUSEMOTION and self.arrastrando:
            if self.arrastrando == "tick":
                self.i = a_tick(e.pos[0])
            else:
                self.velocidad = a_vel(e.pos[0])

    # -------------------------------------------------------- herramientas

    def _construir_herramientas(self):
        y = CABECERA + 20
        self.b_bench = Boton((MARGEN, y, 230, 42), "Medir rendimiento",
                             primario=True)
        self.b_verif = Boton((MARGEN + 242, y, 250, 42),
                             "Buscar race conditions")

        # El benchmark lleva su propio tamano, aparte del que use el
        # visor: por debajo de unas 65 mil celdas los tiempos son tan
        # cortos que la medicion no significa nada.
        y += 62
        self.d_bench_lado = Deslizador((MARGEN, y, 330, 40),
                                       "Cuadricula del benchmark",
                                       128, 2048, 1024, paso=128)
        self.d_bench_ticks = Deslizador((MARGEN + 360, y, 260, 40),
                                        "Ticks por corrida", 5, 40, 20)
        self.deslizadores_bench = [self.d_bench_lado, self.d_bench_ticks]

    def dibujar_herramientas(self):
        ocupado = (self.tarea is not None and not self.tarea.terminada)
        self.b_bench.activo = not ocupado
        self.b_verif.activo = not ocupado
        self.b_bench.dibujar(self.pantalla, self.f_normal)
        self.b_verif.dibujar(self.pantalla, self.f_normal)

        texto(self.pantalla, self.f_chico,
              "Compara 1, 2, 4, 8 y 16 hilos contra la version secuencial.",
              MARGEN + 504, CABECERA + 33, MUTED)

        lado = self.d_bench_lado.valor
        self.d_bench_lado.sufijo = "  ({} celdas)".format(lado * lado)
        for d in self.deslizadores_bench:
            d.dibujar(self.pantalla, self.f_chico, self.f_normal)

        if lado < 256:
            texto(self.pantalla, self.f_chico,
                  "Con esta cuadricula los tiempos son demasiado cortos y la "
                  "medicion no es representativa.",
                  MARGEN + 640, CABECERA + 94, CARNIVORO)

        y = CABECERA + 140
        ancho_consola = ANCHO - MARGEN * 3 - 380
        consola = pygame.Rect(MARGEN, y, ancho_consola, ALTO - y - MARGEN)
        caja(self.pantalla, consola, SUPERFICIE)

        visibles = (consola.h - 24) // 18
        for k, linea in enumerate(self.consola[-visibles:]):
            texto(self.pantalla, self.f_mono, linea[:78], consola.x + 14,
                  consola.y + 12 + k * 18, TINTA2)

        graf = pygame.Rect(consola.right + MARGEN, y, 380,
                           ALTO - y - MARGEN)
        caja(self.pantalla, graf)
        texto(self.pantalla, self.f_chico, "Speedup por hilos", graf.x + 14,
              graf.y + 12, TINTA2)

        if not self.bench:
            texto(self.pantalla, self.f_chico,
                  "Corre una medicion para ver la grafica.", graf.x + 14,
                  graf.y + 40, MUTED)
            return

        px, py = graf.x + 42, graf.y + 44
        pw, ph = graf.w - 62, graf.h - 100
        tope = max(2.0, max(v for _, v in self.bench) * 1.15)

        for k in range(int(tope) + 1):
            yy = py + ph - (k / tope) * ph
            pygame.draw.line(self.pantalla, LINEA, (px, yy), (px + pw, yy))
            texto(self.pantalla, self.f_chico, str(k), px - 8, yy - 9, MUTED,
                  derecha=True)

        n = len(self.bench)
        ancho_barra = min(46, pw // max(1, n) - 12)
        for k, (hilos, sp) in enumerate(self.bench):
            cx = px + (k + 0.5) * pw / n
            alto = (sp / tope) * ph
            r = pygame.Rect(0, 0, ancho_barra, max(2, int(alto)))
            r.midbottom = (int(cx), int(py + ph))
            pygame.draw.rect(self.pantalla, ACENTO, r, border_radius=4)
            texto(self.pantalla, self.f_chico, "{:.2f}".format(sp), int(cx),
                  r.top - 17, TINTA, centrado=True)
            texto(self.pantalla, self.f_chico, str(hilos), int(cx),
                  py + ph + 6, MUTED, centrado=True)

        texto(self.pantalla, self.f_chico, "hilos", px + pw, py + ph + 24,
              MUTED, derecha=True)

    def eventos_herramientas(self, e):
        for d in self.deslizadores_bench:
            d.evento(e)

        if self.b_bench.evento(e):
            self.consola = ["Midiendo, esto tarda un rato..."]
            self.bench = None
            self.tarea = Tarea("bench", [
                BINARIO, "--bench",
                "--filas", str(self.d_bench_lado.valor),
                "--ticks", str(self.d_bench_ticks.valor),
                "--salida", ARCHIVO_BENCH,
            ])
        if self.b_verif.evento(e):
            self.consola = ["Verificando..."]
            self.tarea = Tarea("verificacion", [BINARIO, "--verificar-todo"])

    def cargar_bench(self):
        """Lee el CSV y arma la serie de speedup por cantidad de hilos."""
        try:
            with open(ARCHIVO_BENCH, newline="", encoding="utf-8") as f:
                filas = list(csv.DictReader(f))
        except OSError:
            return
        serie = [
            (int(r["hilos"]), float(r["speedup"]))
            for r in filas if r["modo"] == "paralelo"
        ]
        self.bench = sorted(serie) or None

    # ------------------------------------------------------------ ciclo

    def revisar_tarea(self):
        if self.tarea is None:
            return
        while True:
            try:
                self.consola.append(self.tarea.lineas.get_nowait())
            except queue.Empty:
                break

        if not self.tarea.terminada:
            return

        nombre, codigo = self.tarea.nombre, self.tarea.codigo
        self.tarea = None

        if nombre == "simulacion":
            if codigo == 0:
                try:
                    self.corrida = Corrida(ARCHIVO_CORRIDA)
                    self.i, self.reproduciendo = 0, True
                    self.acumulado = 0.0
                    self.vista = "visor"
                    self.mensaje = ""
                except (OSError, ValueError) as err:
                    self.mensaje = "No se pudo leer la corrida: {}".format(err)
            else:
                self.mensaje = "La simulacion fallo. Revisa los parametros."
        elif nombre == "bench":
            self.cargar_bench()
            self.consola.append("")
            self.consola.append("Medicion terminada.")
        elif nombre == "verificacion":
            self.consola.append("")
            self.consola.append(
                "Sin race conditions." if codigo == 0
                else "Se detectaron problemas.")

    def correr(self):
        if not os.path.exists(BINARIO):
            print("No se encontro el binario. Compila primero con 'make'.")
            return

        while True:
            dt = self.reloj.tick(60) / 1000.0

            for e in pygame.event.get():
                if e.type == pygame.QUIT:
                    pygame.quit()
                    return
                if e.type == pygame.KEYDOWN and e.key == pygame.K_ESCAPE:
                    if self.vista == "visor":
                        self.vista = "config"
                    else:
                        pygame.quit()
                        return

                for clave, boton in self.nav:
                    if boton.evento(e):
                        self.vista = clave

                if self.vista == "config":
                    self.eventos_config(e)
                elif self.vista == "visor":
                    self.eventos_visor(e)
                else:
                    self.eventos_herramientas(e)

            self.revisar_tarea()

            if (self.vista == "visor" and self.reproduciendo
                    and self.corrida is not None):
                total = len(self.corrida.cuadros) - 1
                self.acumulado += dt * self.velocidad
                while self.acumulado >= 1.0:
                    self.acumulado -= 1.0
                    if self.i < total:
                        self.i += 1
                    else:
                        self.reproduciendo = False
                        self.acumulado = 0.0
                        break

            self.pantalla.fill(FONDO)
            self.dibujar_navegacion()
            if self.vista == "config":
                self.dibujar_config()
            elif self.vista == "visor":
                self.dibujar_visor()
            else:
                self.dibujar_herramientas()
            pygame.display.flip()


def main():
    App().correr()


if __name__ == "__main__":
    main()
