# Resourcepack Manager

**ResourcepackManager** é uma aplicação desenvolvida para auxiliar criadores de resourcepacks a gerenciar múltiplos pacotes de recursos. Com um sistema de scripts implementado, o ResourcepackManager pode selecionar uma pasta ou um arquivo para executar ações determinadas pelo usuário.

## Instruções

O sistema de instruções funciona com um arquivo contendo uma lista de comandos que apontam para um arquivo e indicam ao programa o que fazer com ele.

A versão atual (1.0) do ResourcepackManager possui as seguintes ações:

---

### **Copy/Move**:
- O programa seleciona o arquivo/pasta de destino e move/copia para o caminho de destino.
- O programa cria automaticamente as pastas no caminho especificado, caso não existam.
- Se o caminho incluir um nome de arquivo, o programa atualiza automaticamente o nome do arquivo para corresponder ao caminho.

**Exemplos:**
```
> "pack/pack 1.21.4.mcmeta"
   copy "pack.mcmeta";

> "blockstates/"
   copy "assets/minecraft/";
```
---

### **Remove**:
- O programa seleciona o arquivo/pasta de destino e o exclui.

**Exemplos:**
```
> "minecraft:models/item/sweet_berries.json"
   remove;

> "minecraft:textures/entity/equipment/"
   remove;
   ```

---

### **Edit**:
Esta função contém um conjunto de sub-ações relacionadas ao arquivo:

1. **"name"**: altera o nome do arquivo.
   ```
   > "minecraft:models/item/potion.json"
      edit name "bottle_drinkable.json";
   ```

2. **"display" / "texture_path"**: atualiza o respectivo membro do modelo para o valor fornecido no script.
   ```
   > "tools/fishing_rod_cast.json"
      edit texture_path set {
        "0": "item/fishing_rod",
        "particle": "item/fishing_rod"
      };
   ```

3. **"dimensions"**: redimensiona as imagens para as dimensões fornecidas.
   ```
   > "armors/iron_layer_1.png"
      edit dimensions 64x32;
   ```

---

### **Autofill**:
- O programa cria um arquivo que reflete as pastas no resourcepack para ser carregado pelo atlas logger.

**Exemplo:**
```
> "minecraft:atlases/custom.json"
   autofill;
```

---

### **Disassemble**:
- O programa seleciona o arquivo de destino e extrai os grupos para seus próprios modelos de arquivo.

**Exemplo:**
```
> "tools/bow.json"
   disassemble trim "minecraft:models/item/";
```

---

### **Paint**:
- O programa pega a textura e pinta de acordo com a paleta especificada.

**Exemplo:**
```
> "tools/bow.png"
   paint "./materials/wood_map.png" "./materials/wood_material.png";
```

---

### **Permutate_Texture**:
- O programa pega a textura e uma lista de paletas para pintar as duplicatas.

**Exemplo:**
```
> "bricks.png"
   permutate_texture "snowstone_map.png" {"yellow_stone.png", "pink_stone.png", "brown_stone.png", "burgundy.png", "limestone.png", "blue_stone.png", "green_stone.png"};
```
- A duplicata será salva no mesmo local que o arquivo alvo, usando o nome da paleta como prefixo.

---

### **Convert_Overrides**:
- Com a mudança no sistema de overrides, esta ação traduz o puxar do arco, o tempo/ângulo da bússola e do relógio, e o `custom_model_data` para seu próprio arquivo.

**Exemplo:**
```
> "minecraft:models/item/bow.json"
   convert_overrides;
```

- (Não há intenção de adicionar suporte a outros predicados).
