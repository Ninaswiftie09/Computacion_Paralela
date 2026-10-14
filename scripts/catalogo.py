"""Genera el catálogo LaTeX a partir de las firmas actuales del código C."""
from pathlib import Path
import re
out=Path('docs/informe/tablas');out.mkdir(exist_ok=True)
def esc(s):
 return s.replace('\\',r'\textbackslash{}').replace('_',r'\_\allowbreak{}').replace('&',r'\&').replace('%',r'\%').replace('#',r'\#').replace('~',r'\textasciitilde{}')
desc={
'es_numero_sin_signo':'Indica si la cadena contiene solo dígitos y no está vacía.',
'validar_frase':'Comprueba que la frase no esté vacía ni sea mayor que el cifrado. Advierte si es corta.',
'leer_llave':'Escribe la llave validada en el puntero de salida. Devuelve 1 si está entre 0 y 2 elevado a 56 menos 1.',
'leer_bits':'Escribe en bits un valor entre 1 y 56. Devuelve 1 si es válido.',
'leer_archivo':'Devuelve memoria reservada con el archivo y escribe su longitud. Devuelve NULL si falla. El llamador libera el arreglo.',
'busqueda_preparar':'Valida las entradas y completa la estructura de búsqueda. Devuelve 1 si queda lista o 0 si falla.',
'busqueda_liberar':'Libera los arreglos de la búsqueda y deja sus punteros en NULL.',
'imprimir_datos_busqueda':'Muestra programa, archivo, frase, procesos y espacio de búsqueda.',
'imprimir_llave':'Muestra la llave y el texto descifrado, o informa que no se encontró.',
'imprimir_tiempo':'Muestra segundos y velocidad calculada con la cantidad total de intentos.',
'imprimir_texto':'Muestra la cadena y sustituye caracteres de control para conservar la salida de terminal.',
'imprimir_hexadecimal':'Muestra los bytes en formato hexadecimal.',
'segundos_ahora':'Devuelve el tiempo de un reloj monótono en segundos.',
'expandir_llave':'Escribe en llave_des los ocho bytes de llave DES con paridad impar.',
 'transformar':'Modifica los datos con DES ECB según la operación de cifrado o descifrado. La longitud debe ser múltiplo de ocho.',
'des_longitud_rellena':'Devuelve la longitud redondeada al siguiente múltiplo de ocho.',
'des_cifrar':'Sobrescribe los datos con su cifrado DES ECB.',
'des_descifrar':'Sobrescribe los datos con su descifrado DES ECB.',
'des_probar_llave':'Escribe el descifrado en trabajo. Devuelve 1 cuando contiene la frase, o 0 en otro caso. Requiere longitud más uno bytes de trabajo.',
'cifrar_archivo':'Carga el texto, lo cifra y escribe el archivo de salida. Devuelve el código de éxito o error.',
'descifrar_archivo':'Carga el cifrado y muestra su descifrado. Devuelve el código de éxito o error.',
'reservar_memoria':'Reserva cifrado, trabajo y frase para un proceso. Devuelve 1 si todas las reservas tienen éxito.',
'aviso_cerrar':'Completa o cancela la recepción y recibe los avisos restantes según los hallazgos acordados.',
'mpi_preparar_busqueda':'Inicia MPI, verifica soporte de hilos y reparte entradas. Completa busqueda y puede modificar argc y argv. Devuelve 1 si se puede buscar.',
'mpi_imprimir_datos':'Muestra los datos de búsqueda solo desde el proceso 0.',
'aviso_abrir':'Inicializa aviso y abre una recepción no bloqueante de la llave.',
'aviso_llego':'Actualiza la solicitud de aviso. Devuelve 1 si se completó la recepción.',
'aviso_enviar':'Guarda la llave en aviso e inicia los envíos no bloqueantes a los demás procesos. Conserva solicitudes hasta el cierre.',
'mpi_iniciar_cronometro':'Ejecuta una barrera MPI explícita y devuelve el instante de inicio.',
'mpi_cerrar_busqueda':'Completa mensajes, reúne llave y métricas, imprime resultados, libera memoria y finaliza MPI.',
'reservar_trabajos':'Devuelve un arreglo de buffers, uno por hilo. Devuelve NULL si falla y libera las reservas parciales.',
'liberar_trabajos':'Libera los buffers y el arreglo de punteros de los hilos.',
'mezclar':'Devuelve un entero con los bits mezclados para obtener los parámetros de la permutación.',
'leer_semilla':'Escribe una semilla sin signo de 64 bits. Devuelve 1 si es válida o 0 si falla.',
'coordinar':'Responde pedidos y reparte rangos sin repetir. Retira a todos los trabajadores al agotarse el espacio o recibir un hallazgo.',
 'trabajar':'Solicita y recorre bloques. Actualiza en local el hallazgo, su llave y el número de intentos.',
'main':'Procesa los argumentos, ejecuta la operación del programa y devuelve su código de salida.'}
filas=[]
for p in sorted(Path('src').glob('*.c')):
 s=p.read_text(encoding='utf-8')
 pat=r'^(?:static\s+)?((?:unsigned\s+char|int|void|double|uint64_t|size_t)\s+(?:\*+\s*)?)(\w+)\s*\(([^)]*)\)\s*\n\{'
 for m in re.finditer(pat,s,re.M):
  ret=' '.join(m.group(1).split());name=m.group(2);params=' '.join(m.group(3).split())
  if name not in desc: raise ValueError(name)
  filas.append(esc(name)+' ('+esc(p.name)+') & '+esc(params)+' & Retorno: '+esc(ret)+'. '+esc(desc[name])+r' \\'+ '\n'+r'\addlinespace[2pt]'+'\n')
(out/'catalogo.tex').write_text(''.join(filas),encoding='utf-8')
print('Funciones documentadas:',len(filas))
