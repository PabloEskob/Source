// Copyright (C) Developed by Pask, Published by Dark Tower Interactive SRL 2024. All Rights Reserved.

#pragma once

#include "ACFItemTypes.h"
#include "Components/ACFCurrencyComponent.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Items/ACFItem.h"

#include "ACFStorageComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnItemsChanged, const TArray<FBaseItem>&, currentItems);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStorageEmpty);

UCLASS(ClassGroup = (ACF), Blueprintable, meta = (BlueprintSpawnableComponent))
class INVENTORYSYSTEM_API UACFStorageComponent : public UACFCurrencyComponent {
    GENERATED_BODY()

public:
    // Sets default values for this component's properties
    UACFStorageComponent();

protected:
    // Called when the game starts
    virtual void BeginPlay() override;

    UPROPERTY(SaveGame, EditAnywhere, ReplicatedUsing = OnRep_Items, Category = ACF)
    TArray<FBaseItem> Items;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = ACF)
    void OnComponentLoaded();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = ACF)
    void OnComponentSaved();

public:
    UFUNCTION(Server, Reliable, BlueprintCallable, Category = ACF)
    void RemoveItems(const TArray<FBaseItem>& inItems);

    UFUNCTION(Server, Reliable, BlueprintCallable, Category = ACF)
    void RemoveItem(const FBaseItem& inItems);

    UFUNCTION(Server, Reliable, BlueprintCallable, Category = ACF)
    void AddItem(const FBaseItem& inItems);

    UFUNCTION(Server, Reliable, BlueprintCallable, Category = ACF)
    void AddItems(const TArray<FBaseItem>& inItems);

    UFUNCTION(BlueprintCallable, Category = ACF)
    void MoveItemsToInventory(const TArray<FBaseItem>& inItems, UACFEquipmentComponent* equipComp);

    UFUNCTION(BlueprintCallable, Category = ACF)
    void GatherCurrency(float amount, UACFCurrencyComponent* currencyComp);

    UFUNCTION(BlueprintCallable, Category = ACF)
    void GatherAllCurrency(UACFCurrencyComponent* currencyComp);
    /*------------------------ STORAGE -----------------------------------------*/

    UFUNCTION(BlueprintPure, Category = "ACF | Getters")
    class UACFCurrencyComponent* GetPawnCurrencyComponent(const APawn* pawn) const;

    UFUNCTION(BlueprintPure, Category = "ACF | Getters")
    float GetPawnCurrency(const APawn* pawn) const;

    UFUNCTION(BlueprintPure, Category = "ACF | Getters")
    class UACFEquipmentComponent* GetPawnEquipment(const APawn* pawn) const;

    UFUNCTION(BlueprintPure, Category = "ACF | Getters")
    TArray<FInventoryItem> GetPawnInventory(const APawn* pawn) const;

    /*Return the items related to this world Item*/
    UFUNCTION(BlueprintPure, Category = ACF)
    TArray<FBaseItem> GetItems() const
    {
        return Items;
    }

    UFUNCTION(BlueprintPure, Category = ACF)
    bool IsStorageEmpty();

    UPROPERTY(BlueprintAssignable, Category = ACF)
    FOnItemsChanged OnItemChanged;

    UPROPERTY(BlueprintAssignable, Category = ACF)
    FOnStorageEmpty OnStorageEmpty;

    virtual void HandleCurrencyChanged() override;

private:
    UFUNCTION()
    void OnRep_Items();

    void CheckEmpty();
};
