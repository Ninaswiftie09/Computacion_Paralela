# Análisis de la solución secuencial y procesamiento en CPU

## 1. Descripción general

Se utilizó el conjunto de imágenes ASL Alphabet para clasificar 29 señas. Las imágenes se seleccionaron y prepararon en CPU antes de entrenar la red convolucional CNN2. Se trabajó con 600 imágenes por clase, para un total de **17,400 imágenes**, divididas en entrenamiento (12,180), validación (2,610) y prueba (2,610).

## 2. Operaciones identificadas

| No. | Operación | Descripción |
|---|---|---|
| 1 | Selección y división | Se seleccionaron imágenes por clase con semilla 42 y se distribuyeron en conjuntos 70 % / 15 % / 15 %. |
| 2 | Lectura | Se abrieron y decodificaron los archivos de imagen. |
| 3 | Preprocesamiento | Se convirtieron las imágenes a RGB y se ajustaron a 64 × 64 píxeles. |
| 4 | Normalización | Se transformaron los píxeles a valores entre −1 y 1, en formato `float32` y orden NCHW. |
| 5 | Almacenamiento | Se guardaron imágenes y etiquetas en `results/asl_prepared.npz`. |
| 6 | Entrenamiento | Se ejecutaron propagación hacia adelante, cálculo de pérdida y actualización de pesos de CNN2 en CPU. |
| 7 | Validación y prueba | Se evaluó el modelo durante cada época y al finalizar el entrenamiento. |

### Operaciones repetitivas

| Operación | Datos procesados | Repeticiones por ejecución |
|---|---|---:|
| Lectura, ajuste y normalización | 17,400 imágenes | 17,400 en cada preparación |
| Entrenamiento por lotes | 12,180 imágenes, lotes de 64 | 191 lotes × 10 épocas = 1,910 |
| Validación | 2,610 imágenes, lotes de 64 | 41 lotes × 10 épocas = 410 |
| Evaluación final | 2,610 imágenes, lotes de 64 | 41 lotes |

## 3. Configuración y procedimiento

Se utilizó la CNN2 definida en `src/gpu_training.py`, con optimizador Adam, tasa de aprendizaje de 0.001, función de pérdida `CrossEntropyLoss`, tamaño de lote de 64 y semilla 42. El conjunto de datos se preparó tres veces con los mismos parámetros. Después, se realizó un calentamiento de una época, que no se incluyó en las mediciones, y **cinco ejecuciones completas de entrenamiento en CPU**, con diez épocas cada una.

Se registraron los tiempos de selección, procesamiento y guardado del conjunto de imágenes. Para el entrenamiento se midieron la duración por época, el tiempo de entrenamiento y el tiempo total, además de la exactitud de validación.

## 4. Resultados

### 4.1. Preparación de imágenes en CPU

| Ejecución | Selección (s) | Preparación (s) | Guardado (s) | Total (s) |
|---|---:|---:|---:|---:|
| 1 | 6.574 | 308.447 | 151.772 | 466.793 |
| 2 | 16.725 | 66.400 | 149.837 | 232.962 |
| 3 | 17.210 | 64.299 | 19,809.756 | 19,891.265 |
| **Promedio** | **13.503** | **146.382** | **6,703.788** | **6,863.673** |

La preparación de imágenes fue más rápida en las ejecuciones segunda y tercera. Sin embargo, el guardado de la tercera ejecución tuvo una demora extraordinaria. Por ese motivo, el promedio total está muy afectado por ese valor y no representa un tiempo habitual de preparación. Se conservaron las tres mediciones y no se atribuyó la demora a una causa específica sin evidencia adicional.

### 4.2. Entrenamiento CNN2 en CPU

| Ejecución | Entrenamiento (s, aprox.) |
|---|---:|
| 1 | 1,183.52 |
| 2 | 762.63 |
| 3 | 1,261.88 |
| 4 | 775.26 |
| 5 | 1,128.08 |
| **Promedio** | **1,022.27** |

Los tiempos individuales se aproximaron mediante la suma de las épocas mostradas en el registro. El resumen generado por el programa reportó **1,022.273 segundos** como tiempo promedio de entrenamiento y **1,030.721 segundos** como tiempo total promedio, incluyendo configuración y evaluación. La desviación estándar aproximada del tiempo de entrenamiento fue de **236.13 segundos**.

La exactitud de validación al finalizar la décima época fue de **98.81 %** en las cinco ejecuciones. Esta cifra corresponde a validación y no debe interpretarse como exactitud del conjunto de prueba.

### 4.3. Interpretación

Se observó que los tiempos variaron aun cuando se mantuvieron los mismos datos y parámetros. La mayor diferencia apareció en la cuarta época de la tercera ejecución, que tardó 530.68 segundos. Estas variaciones podrían relacionarse con la carga del equipo y otros procesos del sistema, aunque no se identificó una causa definitiva. Por ello, se utilizó el promedio de las cinco ejecuciones y se conservaron los registros originales.

## 5. Decisiones técnicas y conclusión

Se asignaron a CPU la lectura, decodificación, redimensionamiento y normalización de imágenes porque estas tareas requieren gestionar archivos y preparar los datos antes del entrenamiento. El resultado se guardó en un archivo NPZ reutilizable para mantener el mismo conjunto de imágenes en las pruebas posteriores.

La versión CPU permite establecer una referencia de tiempo con la misma CNN2 y configuración previstas para CUDA. **El cálculo de aceleración (speedup) queda pendiente de disponer de las mediciones de GPU**; no se puede concluir todavía que CUDA haya reducido el tiempo de ejecución.

## 6. Evidencias

- `results/asl_split_manifest.csv`: distribución de imágenes.
- `results/cpu_preprocessing_times.csv` y `.json`: mediciones de preparación.
- `results/cpu_training_times.csv` y `.json`: mediciones de entrenamiento.
- `evidence/cpu_training.log`: registro de las cinco ejecuciones.
- `evidence/capturas/cpu_usage.png`: captura del uso de CPU durante el entrenamiento, **si fue tomada**.
