# Análisis de la solución secuencial y preparación en CPU

## Flujo de procesamiento

La solución clasifica imágenes del alfabeto ASL en 29 clases. La preparación de datos se realiza en CPU y produce tres conjuntos reproducibles: entrenamiento, validación y prueba. El modelo de referencia es una red convolucional CNN2; utiliza la misma arquitectura, función de pérdida y parámetros de entrenamiento que la evaluación con CUDA.

| No. | Operación identificada | Descripción |
| --- | --- | --- |
| 1 | Selección de imágenes | Identifica los archivos por clase y selecciona una submuestra reproducible con semilla 42. |
| 2 | División de datos | Organiza las imágenes en entrenamiento (70 %), validación (15 %) y prueba (15 %), por clase. |
| 3 | Lectura y decodificación | Abre archivos JPG mediante Pillow en la CPU. |
| 4 | Preparación de imágenes | Convierte las imágenes a RGB y las redimensiona a 64 × 64 píxeles. |
| 5 | Normalización | Transforma los píxeles a valores entre −1 y 1 y utiliza el orden de canales NCHW. |
| 6 | Almacenamiento | Guarda imágenes y etiquetas en un archivo NPZ compartido por las ejecuciones CPU y GPU. |
| 7 | Entrenamiento CNN2 | Ejecuta propagación hacia adelante, cálculo de pérdida y retropropagación en el dispositivo seleccionado. |
| 8 | Evaluación | Calcula métricas de validación durante cada época y exactitud de prueba al finalizar. |

## Operaciones repetitivas

Sea **N** el número de imágenes seleccionadas, **Ntrain**, **Nval** y **Ntest** sus cantidades por conjunto, **E** el número de épocas y **B** el tamaño del lote. Las cantidades exactas deben tomarse del archivo de resultados generado durante la ejecución.

| Operación repetitiva | Datos que procesa | Repeticiones |
| --- | --- | --- |
| Lectura, conversión RGB y redimensionamiento | Imágenes seleccionadas | N por ejecución de preparación |
| Normalización de píxeles | Imágenes de 64 × 64 × 3 | N por ejecución de preparación |
| Propagación hacia adelante y retropropagación | Lotes de entrenamiento | E × ceil(Ntrain / B) por entrenamiento |
| Evaluación de validación | Lotes de validación | E × ceil(Nval / B) por entrenamiento |
| Evaluación final | Lotes de prueba | ceil(Ntest / B) por entrenamiento |

## Configuración de referencia

- Datos: ASL Alphabet, máximo de 600 imágenes por clase.
- Clases: 29.
- Resolución: 64 × 64 píxeles, RGB.
- Semilla para la selección: 42.
- División de conjuntos: 70 % / 15 % / 15 %.
- Formato compartido: `float32`, NCHW, normalización `((píxel / 255) - 0.5) / 0.5`.
- Modelo: CNN2 de `src/gpu_training.py` (convoluciones, BatchNorm, ReLU, MaxPool, capas densas y Dropout).
- Configuración de entrenamiento: Adam, aprendizaje 0.001, CrossEntropyLoss, 10 épocas y lote de 64, salvo que se documenten cambios en las pruebas.

## Procedimiento de medición

1. Preparar el conjunto de imágenes varias veces usando las mismas condiciones y registrar por separado selección, preparación y escritura.
2. Usar el NPZ preparado en todos los entrenamientos para evitar diferencias entre conjuntos de datos.
3. Ejecutar un calentamiento de una época, excluido de las mediciones.
4. Realizar cinco entrenamientos de referencia en CPU con la misma configuración del entrenamiento GPU.
5. Registrar tiempos de configuración, entrenamiento, evaluación y total; calcular media y desviación estándar.
6. Guardar los registros CSV y JSON y capturar el monitor de uso de CPU durante una ejecución real.

## Resultados

Completar únicamente con mediciones reales obtenidas en el equipo de prueba.

| Medición | Promedio (s) | Desviación estándar (s) |
| --- | ---: | ---: |
| Selección de imágenes | Pendiente | Pendiente |
| Preparación de imágenes | Pendiente | Pendiente |
| Escritura del NPZ | Pendiente | Pendiente |
| Entrenamiento CPU | Pendiente | Pendiente |
| Evaluación final CPU | Pendiente | Pendiente |
| Tiempo total CPU | Pendiente | Pendiente |

## Decisiones técnicas

La CPU se utiliza para la lectura de archivos, decodificación y preparación de las imágenes, porque estas tareas combinan operaciones de entrada/salida, transformaciones y gestión de archivos. El entrenamiento de referencia en CPU conserva el modelo y las condiciones que se usarán para la comparación con CUDA. Los datos preparados se comparten para que las diferencias observadas correspondan principalmente al dispositivo y a los costos asociados a su ejecución. La aceleración y sus limitaciones se analizarán al disponer de los resultados medidos en GPU.

## Evidencias que deben incorporarse

- Registro de preparación: `results/cpu_preprocessing_times.csv` y `results/cpu_preprocessing_times.json`.
- Registro de entrenamiento: `results/cpu_training_times.csv` y `results/cpu_training_times.json`.
- Captura del Administrador de tareas durante el entrenamiento CPU, mostrando utilización y procesadores lógicos.
- Registro de consola de ambas ejecuciones y especificaciones del equipo utilizado.
