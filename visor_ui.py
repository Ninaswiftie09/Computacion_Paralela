"""Controles reutilizables para el visor.

Piezas minimas de interfaz sobre pygame: boton, deslizador y
selector de opciones. Cada una sabe dibujarse y responder a un
evento, nada mas.
"""

import pygame


# ------------------------------------------------------------------ colores
FONDO = (13, 13, 13)
SUPERFICIE = (26, 26, 25)
ELEVADA = (36, 36, 34)
LINEA = (44, 44, 42)
TINTA = (255, 255, 255)
TINTA2 = (195, 194, 183)
MUTED = (137, 135, 129)
ACENTO = (57, 135, 229)
APAGADO = (70, 70, 68)

VACIO = (34, 34, 37)
PLANTA = (25, 158, 112)
HERBIVORO = (57, 135, 229)
CARNIVORO = (217, 89, 38)

COLOR_ESPECIE = {"P": PLANTA, "H": HERBIVORO, "C": CARNIVORO, ".": VACIO}


def texto(sup, fuente, cadena, x, y, color=TINTA, derecha=False,
          centrado=False):
    img = fuente.render(cadena, True, color)
    r = img.get_rect()
    if derecha:
        r.topright = (x, y)
    elif centrado:
        r.midtop = (x, y)
    else:
        r.topleft = (x, y)
    sup.blit(img, r)
    return r


def caja(sup, rect, relleno=SUPERFICIE, borde=LINEA, radio=9):
    pygame.draw.rect(sup, relleno, rect, border_radius=radio)
    if borde:
        pygame.draw.rect(sup, borde, rect, width=1, border_radius=radio)


class Boton:

    def __init__(self, rect, etiqueta, primario=False):
        self.rect = pygame.Rect(rect)
        self.etiqueta = etiqueta
        self.primario = primario
        self.activo = True
        self.encima = False

    def dibujar(self, sup, fuente):
        if not self.activo:
            relleno, borde, color = SUPERFICIE, LINEA, APAGADO
        elif self.primario:
            relleno = (80, 155, 240) if self.encima else ACENTO
            borde, color = relleno, (255, 255, 255)
        else:
            relleno = ELEVADA if self.encima else SUPERFICIE
            borde, color = LINEA, TINTA

        caja(sup, self.rect, relleno, borde, 8)
        img = fuente.render(self.etiqueta, True, color)
        sup.blit(img, img.get_rect(center=self.rect.center))

    def evento(self, e):
        if e.type == pygame.MOUSEMOTION:
            self.encima = self.rect.collidepoint(e.pos)
        if (e.type == pygame.MOUSEBUTTONDOWN and e.button == 1
                and self.activo and self.rect.collidepoint(e.pos)):
            return True
        return False


class Deslizador:
    """Deslizador de valor entero o real con etiqueta y lectura."""

    def __init__(self, rect, etiqueta, minimo, maximo, valor,
                 entero=True, sufijo="", paso=None):
        self.rect = pygame.Rect(rect)
        self.etiqueta = etiqueta
        self.minimo = minimo
        self.maximo = maximo
        self.valor = valor
        self.entero = entero
        self.sufijo = sufijo
        self.paso = paso
        self.arrastrando = False
        self.activo = True

    @property
    def barra(self):
        return pygame.Rect(self.rect.x, self.rect.y + 26, self.rect.w, 14)

    def _fraccion(self):
        if self.maximo == self.minimo:
            return 0.0
        return (self.valor - self.minimo) / (self.maximo - self.minimo)

    def _desde_x(self, mx):
        b = self.barra
        t = (mx - b.x) / max(1, b.w)
        t = max(0.0, min(1.0, t))
        v = self.minimo + t * (self.maximo - self.minimo)
        if self.paso:
            v = round(v / self.paso) * self.paso
        v = int(round(v)) if self.entero else round(v, 3)
        return max(self.minimo, min(self.maximo, v))

    def texto_valor(self):
        if self.entero:
            return "{}{}".format(self.valor, self.sufijo)
        return "{:.2f}{}".format(self.valor, self.sufijo)

    def dibujar(self, sup, f_chico, f_normal):
        color_et = MUTED if self.activo else APAGADO
        color_val = TINTA if self.activo else APAGADO
        texto(sup, f_chico, self.etiqueta, self.rect.x, self.rect.y, color_et)
        texto(sup, f_normal, self.texto_valor(), self.rect.right,
              self.rect.y - 2, color_val, derecha=True)

        b = self.barra
        pygame.draw.rect(sup, LINEA, b, border_radius=7)
        frac = self._fraccion()
        if frac > 0:
            pygame.draw.rect(
                sup, ACENTO if self.activo else APAGADO,
                pygame.Rect(b.x, b.y, int(b.w * frac), b.h), border_radius=7)
        pygame.draw.circle(
            sup, (255, 255, 255) if self.activo else APAGADO,
            (int(b.x + b.w * frac), b.centery), 8)

    def evento(self, e):
        if not self.activo:
            return False
        zona = self.barra.inflate(0, 18)
        if e.type == pygame.MOUSEBUTTONDOWN and e.button == 1:
            if zona.collidepoint(e.pos):
                self.arrastrando = True
                self.valor = self._desde_x(e.pos[0])
                return True
        if e.type == pygame.MOUSEBUTTONUP:
            self.arrastrando = False
        if e.type == pygame.MOUSEMOTION and self.arrastrando:
            self.valor = self._desde_x(e.pos[0])
            return True
        return False


class Segmentado:
    """Selector de una opcion entre varias, en linea."""

    def __init__(self, rect, etiqueta, opciones, indice=0):
        self.rect = pygame.Rect(rect)
        self.etiqueta = etiqueta
        self.opciones = opciones
        self.indice = indice

    @property
    def valor(self):
        return self.opciones[self.indice]

    def _celdas(self):
        y = self.rect.y + 24
        alto = self.rect.h - 24
        ancho = self.rect.w // len(self.opciones)
        return [
            pygame.Rect(self.rect.x + i * ancho, y, ancho, alto)
            for i in range(len(self.opciones))
        ]

    def dibujar(self, sup, f_chico, f_normal):
        texto(sup, f_chico, self.etiqueta, self.rect.x, self.rect.y, MUTED)
        for i, r in enumerate(self._celdas()):
            activo = (i == self.indice)
            caja(sup, r.inflate(-4, 0),
                 ACENTO if activo else SUPERFICIE,
                 ACENTO if activo else LINEA, 8)
            img = f_normal.render(
                self.opciones[i], True,
                (255, 255, 255) if activo else TINTA2)
            sup.blit(img, img.get_rect(center=r.center))

    def evento(self, e):
        if e.type == pygame.MOUSEBUTTONDOWN and e.button == 1:
            for i, r in enumerate(self._celdas()):
                if r.collidepoint(e.pos):
                    self.indice = i
                    return True
        return False
