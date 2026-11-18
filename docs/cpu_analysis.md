# Análisis de la solución secuencial y procesamiento en CPU

Este documento resume la parte de CPU del proyecto. El análisis completo, con todas las tablas y figuras, está en `docs/informe/informe.pdf`.

## 1. Descripción general

Se utilizó el conjunto ASL Alphabet para clasificar 29 señas. La selección original del Laboratorio 3 toma 600 imágenes por clase con semilla 42, para un total de 17,400 imágenes divididas en entrenamiento (12,180), validación (2,610) y prueba (2,610). El modelo es `cnn_improved_a` del Laboratorio 3, la configuración que eligió el ajuste original, traducida a PyTorch en `src/gpu_training.py`.

## 2. Operaciones identificadas

| No. | Operación | Descripción |
|---|---|---|
| 1 | Selección y división | Se listan los archivos de cada clase, se mezclan con semilla 42 y se reparten en 70 %, 15 % y 15 %. |
| 2 | Lectura | Se abren y decodifican los archivos JPEG. |
| 3 | Preprocesamiento | Las imágenes se convierten a RGB y se redimensionan a 64 × 64 píxeles con LANCZOS. |
| 4 | Normalización | Los píxeles se dividen entre 255, como en el Laboratorio 3, y se ordenan en formato NCHW. |
| 5 | Almacenamiento | Imágenes y etiquetas se guardan en un archivo NPZ. |
| 6 | Entrenamiento | Propagación hacia adelante, cálculo de la pérdida, retropropagación y actualización con Adam. |
| 7 | Validación y prueba | El modelo se evalúa al final de cada época y al terminar el entrenamiento. |

### Operaciones repetitivas

| Operación | Datos procesados | Repeticiones por ejecución |
|---|---|---:|
| Lectura, ajuste y normalización | 17,400 imágenes | 17,400 en cada preparación |
| Entrenamiento por lotes | 12,180 imágenes, lotes de 64 | 191 lotes × 10 épocas = 1,910 |
| Validación | 2,610 imágenes, lotes de 64 | 41 lotes × 10 épocas = 410 |
| Evaluación final | 2,610 imágenes, lotes de 64 | 41 lotes |

## 3. Procedimiento

Todas las mediciones definitivas se hicieron con `src/benchmark.py` en la misma laptop que tiene la GPU: Intel Core i9-13900HX con 24 núcleos (8 de alto rendimiento y 16 de eficiencia) y 32 hilos, y 15.7 GB de RAM. La preparación se ejecutó tres veces con 1, 2, 4, 8, 16, 24 y 32 procesos. El guardado y la carga del NPZ se midieron tres veces. El entrenamiento en CPU tuvo una época de calentamiento que no se mide y cinco ejecuciones de diez épocas con lotes de 64, Adam con tasa de aprendizaje de 0.001 y semilla 42.

La preparación en paralelo está en `prepare_arrays` de `src/cpu_pipeline.py`, con el parámetro `workers`. Las rutas se reparten en lotes de 200 entre procesos independientes. Todas las variantes producen exactamente los mismos datos, comprobados con una huella SHA-256.

## 4. Resultados

### 4.1. Preparación de imágenes

| Fase | Tiempo (s) |
|---|---:|
| Selección del split | 12.59 ± 0.28 |
| Preparación con 1 proceso | 14.17 ± 2.53 |
| Preparación con 8 procesos | 3.38 ± 0.14, más 1.61 de arranque |
| Preparación con 16 procesos | 2.02 ± 0.16, más 3.44 de arranque |
| Guardado del NPZ | 32.38 ± 2.62 |
| Carga del NPZ | 2.19 ± 0.43 |

Solo con el trabajo, la preparación llega a 7.02x con 16 procesos. Si se cuenta el arranque de los procesos, el mejor resultado es 2.84x con 8 procesos. La primera preparación tardó 17.07 s y las siguientes cerca de 12.7 s porque los archivos ya estaban en el caché del sistema.

### 4.2. Entrenamiento en CPU

| Versión | Corridas | Entrenamiento (s) | Exactitud de prueba |
|---|---:|---:|---:|
| Keras, código original del Laboratorio 3 | 2 | 192.16 ± 15.08 | 98.05 % |
| PyTorch, misma red traducida | 5 | 176.00 ± 6.63 | 94.44 % |

Las dos versiones tienen 164,837 parámetros entrenables y siguen la misma curva de aprendizaje. La diferencia de exactitud viene de la última época, porque sin ajuste de la tasa de aprendizaje la exactitud oscila entre épocas. En la novena época PyTorch llegó a 97.28 % de validación. Las cinco corridas en CPU dieron el mismo resultado.

## 5. Decisiones técnicas

La lectura, la decodificación, la redimensión y la normalización se mantuvieron en el CPU porque tratan archivos pequeños e independientes y la decodificación de JPEG toma muchas decisiones por byte. Ese trabajo se reparte bien entre procesos. Se usaron procesos y no hilos porque el intérprete de Python solo ejecuta código de Python en un hilo a la vez. El NPZ permite que las versiones de CPU y GPU entrenen con exactamente los mismos datos.

## 6. Medición anterior

Antes de las mediciones definitivas se midió una red llamada CNN2 en otra laptop, con 4 hilos de PyTorch y normalización entre −1 y 1. El entrenamiento promedio fue de 1,022.27 s, y uno de los guardados del NPZ tardó 19,809.76 s. Ese archivo se escribía dentro de una carpeta de OneDrive, que es la causa más probable de la demora, porque en la laptop de las mediciones definitivas el mismo guardado fuera de OneDrive tarda cerca de 30 s. Esos tiempos no se usaron para calcular la aceleración porque vienen de otro equipo y de un modelo que no pertenece al Laboratorio 3. Sus archivos se conservan como registro en `results/cpu_preprocessing_times.*`, `results/cpu_training_times.*`, `evidence/cpu_training.log` y `evidence/capturas/cpu_usage.png`.

## 7. Evidencias

Las mediciones definitivas están en `results/benchmark`. `corridas_preparacion.csv` y `corridas_almacenamiento.csv` tienen los tiempos de la preparación, y `corridas_entrenamiento.csv` los del entrenamiento. `monitor_cpu.csv` registra el uso de cada núcleo cada segundo, y la figura `figuras/uso_cpu_nucleos.png` lo muestra. Las capturas del Administrador de tareas están en `evidence/capturas/cpu_preparacion_paralela.png` y `evidence/capturas/cpu_entrenamiento_32_hilos.png`. La referencia en Keras está en `results/keras_referencia`.
